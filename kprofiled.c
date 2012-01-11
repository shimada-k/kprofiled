#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>	/* calloc(3), exit(3), EXIT_SUCCESS */
#include <pthread.h>	/* pthread_exit(3), pthread_join(3), pthread_create(3) */
#include <unistd.h>	/* daemon(3), sleep(3) */
#include <sys/types.h>
#include <sys/stat.h>	/* mkdir(2) */
#include <signal.h>	/* getpid(2) */

#include <stdbool.h>

#include "kprofiled.h"

char *wd_path;
int nr_cpus;

void *trace_lb_entry_worker(void *arg);
void *trace_actually_load_worker(void *arg);
void *l3miss_worker(void *arg);
void *kpreport_worker(void *arg);

/* シグナルを待ち受けて動作する関数 */
void trace_actually_load_handler(void);
void trace_lb_entry_handler(void);

/*
	リソースを確保する関数
*/
bool kprofiled_alloc_resources(void)
{
	return true;
}

/*
	リソースを解放する関数
	@called 呼び出し元の関数名
*/
void kprofiled_free_resources(const char *called)
{
	NOTICE_MESSAGE("%s calls kprofiled_free_resources()\n", called);
	closelog();
}

/*
	初期化関数
	return　成功：true　失敗：false
*/
bool kprofiled_init(void)
{
	FILE *f = NULL;
	char path[STR_PATH_MAX];

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);	/* CPU数を取得 */

	openlog("kprofiled", LOG_PID, LOG_DAEMON);

#ifdef LOAD_KERNEL_BUILD
	daemon(0, 0);	/* カーネルビルドを負荷に使うならデーモン化 */
#endif

	/*
		これ以降は子プロセスで実行される
	*/

	snprintf(path, STR_PATH_MAX, "%s%s", wd_path, "kprofiled.pid");

	if(!(f = fopen(path, "w"))){	/* .pidファイルを作成 */
		ERR_MESSAGE("%s cannot open pid file", log_err_prefix(kprofiled_init));
		return false;
	}

	fprintf(f, "%d", getpid());	/* ここはdaemon(3)の後に実行されないといけない */
	fclose(f);		/* *.pidファイルをクローズ */

	if(kprofiled_alloc_resources() == false){
		return false;
	}

	NOTICE_MESSAGE("starting kprofiled.");

	return true;
}

/*
	終了時に呼び出される関数
*/
void kprofiled_final(void)
{
	char pid_path[STR_PATH_MAX];

	snprintf(pid_path, STR_PATH_MAX, "%skprofiled.pid", wd_path);
	remove(pid_path);	/* remove kprofiled.pid */

	kprofiled_free_resources(__func__);
}

/*
	main関数 シグナルはメインスレッドが受信する
*/
int main(int argc, char *argv[])
{
	int signo;
	struct sigaction act;
	pthread_t trace_lb_entry, trace_actually_load, l3miss, kpreport;

	if(argc == 1){
		wd_path = DEFAULT_WD;

		if(kprofiled_init() == false){
			ERR_MESSAGE("%s failed", log_err_prefix(kprofiled_init));
			exit(EXIT_FAILURE);
		}
	}
	else if(argc == 2){

		FILE *wd = NULL;

		if((wd = fopen(&argv[1][8], "r"))){	/* work_dir="xxx" */
			fclose(wd);
		}
		else{
			if(mkdir(&argv[1][8], 0777) == -1)	/* ワーキングディレクトリを作成する */
				exit(EXIT_FAILURE);
		}
		wd_path = &argv[1][8];

		if(kprofiled_init() == false){
			ERR_MESSAGE("%s kprofiled_init() failed", log_err_prefix(kprofiled_init));
			exit(EXIT_FAILURE);
		}
	}
	else{
		ERR_MESSAGE("%s invalid argument", log_err_prefix(main));
		exit(EXIT_FAILURE);
	}

	/* SIGTERMをブロックするための設定 */
	sigemptyset(&act.sa_mask);

	if(sigaddset(&act.sa_mask, SIGTERM) != 0){
		ERR_MESSAGE("%s sigaddset() failed", log_err_prefix(main));
		return 1;
	}

	if(sigaddset(&act.sa_mask, SIGUSR1) != 0){
		ERR_MESSAGE("%s sigaddset() failed", log_err_prefix(main));
		return 1;
	}

	if(sigaddset(&act.sa_mask, SIGUSR2) != 0){
		ERR_MESSAGE("%s sigaddset() failed", log_err_prefix(main));
		return 1;
	}

	if(sigprocmask(SIG_BLOCK, &act.sa_mask, NULL) != 0){
		ERR_MESSAGE("%s sigprocmask() failed", log_err_prefix(main));
		return 1;
	}

	/* スレッドの生成 */

#if 1
	if(pthread_create(&trace_lb_entry, NULL, trace_lb_entry_worker, NULL) != 0){
		ERR_MESSAGE("%s pthread_create() failed", log_err_prefix(main));
	}
#endif

#if 1
	if(pthread_create(&trace_actually_load, NULL, trace_actually_load_worker, NULL) != 0){
		ERR_MESSAGE("%s pthread_create() failed", log_err_prefix(main));
	}
#endif

#if 1
	if(pthread_create(&l3miss, NULL, l3miss_worker, NULL) != 0){
		ERR_MESSAGE("%s pthread_create() failed", log_err_prefix(main));
	}
#endif

#if 1
	if(pthread_create(&kpreport, NULL, kpreport_worker, NULL) != 0){
		ERR_MESSAGE("%s pthread_create() failed", log_err_prefix(main));
	}
#endif


	/* シグナルが届くまでmainスレッドは無限ループ */
	while(1){
		int _break = 0;

		if(sigwait(&act.sa_mask, &signo) == 0){	/* シグナルが受信できたら */
			switch(signo) {
				case SIGTERM:
					NOTICE_MESSAGE("kprofiled:sigterm recept\n");
					_break = 1;
				break;
				case SIGUSR1:
					trace_lb_entry_handler();
				break;
				case SIGUSR2:
					trace_actually_load_handler();
				break;
			}


			if(_break){
				break;
			}
		}
	}

	pthread_cancel(trace_lb_entry);
	pthread_cancel(trace_actually_load);
	pthread_cancel(l3miss);
	pthread_cancel(kpreport);

	pthread_join(trace_lb_entry, NULL);
	pthread_join(trace_actually_load, NULL);
	pthread_join(l3miss, NULL);
	pthread_join(kpreport, NULL);

	NOTICE_MESSAGE("stopping kprofiled");

	kprofiled_final();

	return 0;
}



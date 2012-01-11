#include <stdio.h>
#include <stdlib.h>		/* calloc(3) */
#include <unistd.h>		/* open(2), sleep(3) */
#include <sys/types.h>
#include <signal.h>		/* getpid(2) */

#include <pthread.h>
#include <syslog.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include "kprofiled.h"



/**********************************************************
*
*	イベント固有の設定
*	扱うイベントに合わせて変更が必要な箇所
*
**********************************************************/

#define DEVICE_FILE			"/dev/trace_lb_entry"
#define FLUSH_PERIOD			500	/* デバイス側でCLISTに対してポーリングする周期（ミリ秒で指定） */
#define CLIST_NR_NODE		16	/* CLISTでのノード数 */
#define CLIST_NODE_NR_COMPOSED	600	/* CLISTで1ノードに含まれるオブジェクト数 */
#define READ_NR_OBJECT		3000	/* デバイスファイルに読みにいく際の最大オブジェクト数 */

/*
	注意！	・ユーザ空間とやりとりする構造体はパッディングが発生しない構造にすること
		・この構造体のメンバのsizeof()の合計がsizeof(struct object)と一致するようにすること
		・この構造体の定義はドライバ側のものと同一であること
*/
struct object{
	int src_cpu, dst_cpu;
	long pid;
	long sec, usec;
};

/**********************************************************
	イベント固有の設定ここまで
**********************************************************/


#define IO_MAGIC				'k'
#define IOC_USEREND_NOTIFY			_IO(IO_MAGIC, 0)	/* ユーザアプリ終了時 */
#define IOC_SIGRESET_REQUEST		_IO(IO_MAGIC, 1)	/* send_sig_argをリセット要求 */
#define IOC_SUBMIT_SPEC			_IOW(IO_MAGIC, 2, struct signal_spec *)	/* ユーザからのパラメータ設定 */

struct ioc_submit_spec{
	int pid;
	int signo, flush_period;
	int nr_node, node_nr_composed;
	int dummy;	/* padding防止のための変数 */
};

static int dev, out;	/* ファイルディスクリプタ */
static int count;
static void *buffer;

/*
	mainスレッドでsigwait()して動作する関数
*/
void trace_lb_entry_handler(void)
{
	ssize_t size;

	/* カーネルのメモリを読む */
	size = read(dev, buffer, sizeof(struct object) * READ_NR_OBJECT);
	/* ファイルに書き出す */
	write(out, buffer, (size_t)size);

	NOTICE_MESSAGE("read(オブジェクト数): %d\n", (int)(size / sizeof(struct object)));

	count += (int)(size / sizeof(struct object));

	lseek(dev, 0, SEEK_SET);
}

/*
	リソースを確保する関数
*/
static int trace_lb_entry_alloc_resources(void)
{
	char data_path[STR_PATH_MAX];

	snprintf(data_path, STR_PATH_MAX, "%s%s", wd_path, "trace_lb_entry.data");

	dev = open(DEVICE_FILE, O_RDONLY);

	if(dev < 0){
		return 0;
	}

	out = open(data_path, O_CREAT|O_WRONLY|O_TRUNC);

	if(out < 0){
		return 0;
	}

	buffer = (struct object *)calloc(READ_NR_OBJECT, sizeof(struct object));

	if(buffer == NULL){
		return 0;
	}

	return 1;
}

/*
	リソースを解放する関数
	@called 呼び出し元の関数名
	return 成功:1 失敗:0
*/
static int trace_lb_entry_free_resources(const char *called)
{
	NOTICE_MESSAGE("%s calls trace_lb_entry_free_resources()\n", called);

	if(close(dev) < 0){
		return 0;
	}

	if(close(out) < 0){
		return 0;
	}

	return 1;
}


/*
	初期化関数
	返り値　成功:1 失敗:0
*/
static int trace_lb_entry_init(void)
{
	struct ioc_submit_spec submit_spec;
	//struct sigaction act;

	if(trace_lb_entry_alloc_resources() == 0){
		ERR_MESSAGE("%s failed", log_err_prefix(trace_lb_entry_alloc_resources));
		return 0;
	}

	/* デバイスの準備 */
	submit_spec.pid = (int)getpid();
	submit_spec.signo = SIGUSR1;
	submit_spec.flush_period = FLUSH_PERIOD;
	submit_spec.nr_node = CLIST_NR_NODE;
	submit_spec.node_nr_composed = CLIST_NODE_NR_COMPOSED;

	if(ioctl(dev, IOC_SUBMIT_SPEC, &submit_spec) < 0){
		ERR_MESSAGE("%s IOC_SUBMIT_SPEC", log_err_prefix(trace_lb_entry_init));
		return 0;
	}

	return 1;
}

/*
	終了時に呼ばれる関数
	@arg 引数 pthread_cleanup_pushで指定される
*/
static void trace_lb_entry_final(void *arg)
{
	int nr_wcurr, grain;
	ssize_t size;

	/* カーネル側に終了通知を送る */
	if(ioctl(dev, IOC_USEREND_NOTIFY, &nr_wcurr) < 0){
		ERR_MESSAGE("%s IOC_USEREND_NOTIFY", log_err_prefix(trace_lb_entry_final));
		trace_lb_entry_free_resources(__func__);
	}

	NOTICE_MESSAGE("wcurr_len:%d\n", nr_wcurr);

	/* clist_pull_end()でpull残しがないように大きい方でメモリを確保 */
	if(nr_wcurr >= READ_NR_OBJECT){
		/* 一端freeして、再度calloc */
		free(buffer);
		buffer = calloc(nr_wcurr, sizeof(struct object));

		if(buffer == NULL){
			ERR_MESSAGE("%s buffer is NULL", log_err_prefix(trace_lb_entry_final));
		}

		grain = nr_wcurr;
	}
	else{
		/* bufferをそのまま使うのでcalloc無し */
		grain = READ_NR_OBJECT;
	}

	/* grainだけひたすら読んでread(2)が0を返したらbreakする */
	while(1){
		/* カーネルのメモリを読む */
		size = read(dev, buffer, sizeof(struct object) * grain);

		if(size == 0){	/* ここを通るということはclist_benchmark側がSIGRESET_ACCEPTEDになったということ */
			break;
		}

		/* ファイルに書き出す */
		write(out, buffer, (size_t)size);

		NOTICE_MESSAGE("read(オブジェクト数): %d\n", (int)(size / sizeof(struct object)));

		count += (int)(size / sizeof(struct object));

		lseek(dev, 0, SEEK_SET);
	}

	/* ベンチマーク結果を出力 */
	NOTICE_MESSAGE("------------ベンチマーク結果---------------\n");
	NOTICE_MESSAGE("入出力オブジェクト総数：%d\n", count);
	NOTICE_MESSAGE("読み込み粒度（オブジェクト数）：%d\n", READ_NR_OBJECT);
	NOTICE_MESSAGE("clistのノード数：%d\n", 10);
	NOTICE_MESSAGE("clistのノードに含まれるオブジェクト数：%d\n", 100);

	if(trace_lb_entry_free_resources(__func__) == 0){
		exit(EXIT_FAILURE);
	}
}

void *trace_lb_entry_worker(void *arg)
{
	if(trace_lb_entry_init() == 0){
		ERR_MESSAGE("%s failed", log_err_prefix(trace_lb_entry_init));
	}

	pthread_cleanup_push(trace_lb_entry_final, NULL);

	while(1){
		sleep(1);
		pthread_testcancel();
		/* もしSIGUSR1を受信したら, trace_lb_entry_handler()がcallされる */
	}

	pthread_exit(NULL);
	pthread_cleanup_pop(1);
}


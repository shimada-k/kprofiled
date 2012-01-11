#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>	/* sleep(3) */
#include <syslog.h>

#include "kprofiled.h"
#include "lib/msr.h"

#include "lib/msr_address.h"

#define USE_NR_MSR	4	/* いくつのMSRを使ってイベントを計測するか */
#define MAX_RECORDS	1200	/* 最大何回計測するか */

static FILE *tmp_fp[USE_NR_MSR];
static FILE *csv;

/*
	前回のデータとの差分を取る関数 ライブラリ側で呼び出される
	msr_handleに格納される関数（scope==thread or core用）
	@handle_id mh_ctl.handles[]の添字
	@val MSRを計測した生データ
	return 失敗:-1 成功:0
*/
int sub_record_single(int handle_id, unsigned long long *val)
{
	int num, skip = 0;
	unsigned long long val_last;

	/*-- tmp_fpはcloseすると削除されるので、openとcloseはalloc,freeで行うこととする --*/

	fseek(tmp_fp[handle_id], 0, SEEK_SET);

	/* 過去のvalをtmp_fpから読み込む */
	if((num = fread(&val_last, sizeof(unsigned long long), 1, tmp_fp[handle_id])) != 1){
		skip = 1;	/* データがないので今回はtempファイルに書き込むだけ */
	}

	fseek(tmp_fp[handle_id], 0, SEEK_SET);

	/* 現在のcpu_valを書き込む */
	fwrite(val, sizeof(unsigned long long), 1, tmp_fp[handle_id]);

	fseek(tmp_fp[handle_id], 0, SEEK_SET);

	/* MSRの回数は増えることはあっても減ることはないのでここは絶対0以上 */
	*val -= val_last;

	if(skip){
		return -1;
	}
	else{
		return 0;
	}
}

/*
	前回のデータとの差分を取る関数 ライブラリ側で呼び出される
	msr_handleに格納される関数（scope==thread or core用）
	@handle_id mh_ctl.handles[]の添字
	@val MSRを計測した生データ
	return 失敗:-1 成功:0
*/
int sub_record_multi(int handle_id, unsigned long long *val)
{
	int nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	int skip = 0;

	unsigned long long val_last[nr_cpus];
	int i;
	int num;

	/*-- tmp_fpはcloseすると削除されるので、openとcloseはalloc,freeで行うこととする --*/

	fseek(tmp_fp[handle_id], 0, SEEK_SET);

	/* 過去のvalをtmp_fpから読み込む */
	if((num = fread(val_last, sizeof(unsigned long long), nr_cpus, tmp_fp[handle_id])) != nr_cpus){
		skip = 1;	/* データがないので今回はtempファイルに書き込むだけ */
	}

	fseek(tmp_fp[handle_id], 0, SEEK_SET);

	/* 現在のcpu_valを書き込む */
	fwrite(val, sizeof(unsigned long long), nr_cpus, tmp_fp[handle_id]);

	fseek(tmp_fp[handle_id], 0, SEEK_SET);

	for(i = 0; i < nr_cpus; i ++){
		/* MSRの回数は増えることはあっても減ることはないのでここは絶対0以上 */
		val[i] -= val_last[i];
	}

	if(skip){
		return -1;
	}
	else{
		return 0;
	}
}

/*
	リソースを確保する関数
	成功:1 失敗:0
*/
static int l3miss_alloc_resources(void)
{
	int i;
	char path[STR_PATH_MAX];

	snprintf(path, STR_PATH_MAX, "%s%s", wd_path, "l3miss.csv");

	if(!(csv = fopen(path, "w+"))){	/* l3miss.csvを作成 */
		return 0;	/* エラー */
	}

	/* tempファイルをオープン */
	for(i = 0; i < USE_NR_MSR; i++){
		if((tmp_fp[i] = tmpfile()) == NULL){
			return 0;
		}
	}

	return 1;
}

/*
	各種レジスタを設定する関数
*/
static void set_register(void)
{
	int nr_ia32_pmcs, nr_unc_pmcs;
	union IA32_PERFEVTSELx reg;
	reg.full = 0;

	/* PerfGlobalCtrlレジスタを設定 */
	nr_ia32_pmcs = set_IA32_PERF_GLOBAL_CTRL();
	nr_unc_pmcs = set_UNC_PERF_GLOBAL_CTRL();

	NOTICE_MESSAGE("%d ia32_pmcs %d unc_pmcs registered.\n", nr_ia32_pmcs, nr_unc_pmcs);

	/* PERFEVENTSELの設定 */

	reg.split.EvtSel = EVENT_MEM_LOAD_RETIRED_MISS;
	reg.split.UMASK = UMASK_MEM_LOAD_RETIRED_MISS;
//	reg.split.EvtSel = EVENT_LONGEST_CACHE_LAT;
//	reg.split.UMASK = UMASK_LONGEST_CACHE_LAT_MISS;

	reg.split.EN = 1;

	reg.split.USER = 1;
	reg.split.OS = 0;
	set_IA32_PERFEVTSEL(IA32_PERFEVENTSEL0, &reg);	/* ユーザ空間のみ */

	reg.split.USER = 0;
	reg.split.OS = 1;
	set_IA32_PERFEVTSEL(IA32_PERFEVENTSEL1, &reg);	/* OS空間のみ */

	reg.split.USER = 1;
	reg.split.OS = 1;
	set_IA32_PERFEVTSEL(IA32_PERFEVENTSEL2, &reg);	/* ユーザ・OS両方 */

	//set_UNC_PERFEVTSEL_handy(IA32_PERFEVENTSEL0, UMASK_LONGEST_CACHE_LAT_MISS, EVENT_LONGEST_CACHE_LAT);
	//set_UNC_PERFEVTSEL_handy(IA32_PERFEVENTSEL1, UMASK_LONGEST_CACHE_LAT_REFERENCE, EVENT_LONGEST_CACHE_LAT);


	/* UNC_PERFEVENTSELの設定 */

	set_UNC_PERFEVTSEL_handy(MSR_UNCORE_PERFEVTSEL0, UNC_L3_MISS_READ_UMASK, UNC_L3_MISS_EVTNUM);
}

/*
	l3miss初期化関数
	成功:1 失敗:0
*/
static int l3miss_init(void)
{
	set_register();

	if((l3miss_alloc_resources()) == 0){	/* tempファイルをオープンするだけ */
		ERR_MESSAGE("%s failed", log_err_prefix(l3miss_alloc_resources));
		return 0;
	}

	/* MAX_RECORDS回、USE_NR_MSR個のMSRを使って計測する。という指定 */

	return 1;
}

/*
	リソースの解放を行う関数
	成功:1 失敗:0
*/
static int l3miss_free_resources(void)
{
	int i;

	for(i = 0; i < USE_NR_MSR; i++){
		if(fclose(tmp_fp[i]) == EOF){
			return 0;
		}
	}

	return 1;
}


/*
	終了時に呼ばれる関数
*/
static void l3miss_final(void *arg)
{
	int i;

	flushHandleRecords();

	if(l3miss_free_resources() == 0){
		ERR_MESSAGE("%s failed", log_err_prefix(l3miss_free_resources));
		exit(EXIT_FAILURE);
	}

	for(i = 0; i < USE_NR_MSR; i++){
		deactivateHandle((MHANDLE *)arg + i);
	}

	/* 後始末 */
	termHandleController();
}

void *l3miss_worker(void *arg)
{
	MHANDLE *handles = NULL;

	if(l3miss_init() == 0){
		ERR_MESSAGE("%s failed", log_err_prefix(l3miss_init));
	}

	if((handles = initHandleController(csv, MAX_RECORDS, USE_NR_MSR)) == NULL){
		ERR_MESSAGE("%s failed", log_err_prefix(init_handle_controller));
	}

	if(activateHandle(&handles[0], "MEM_LOAD_RETIRED.MISS USER only", MSR_SCOPE_THREAD, IA32_PMC0, sub_record_multi) == -1){
		ERR_MESSAGE("%s failed", log_err_prefix(activateHandle));
	}

	if(activateHandle(&handles[1], "MEM_LOAD_RETIRED.MISS OS only", MSR_SCOPE_THREAD, IA32_PMC1, sub_record_multi) == -1){
		ERR_MESSAGE("%s failed", log_err_prefix(activateHandle));
	}

	if(activateHandle(&handles[2], "MEM_LOAD_RETIRED.MISS both ring", MSR_SCOPE_THREAD, IA32_PMC2, sub_record_multi) == -1){
		ERR_MESSAGE("%s failed", log_err_prefix(activateHandle));
	}

	if(activateHandle(&handles[3], "UNC_L3_MISS.READ", MSR_SCOPE_PACKAGE, MSR_UNCORE_PMC0, sub_record_single) == -1){
		ERR_MESSAGE("%s failed", log_err_prefix(activateHandle));
	}

	pthread_cleanup_push(l3miss_final, (void *)handles);

	while(1){

		pthread_testcancel();

		sleep(1);

		if(getEventValues() == -1){	/* MAX_RECORDS以上計測した */
			NOTICE_MESSAGE("l3miss : time over");
			break;
		}
	}

	pthread_exit(NULL);
	pthread_cleanup_pop(1);
}


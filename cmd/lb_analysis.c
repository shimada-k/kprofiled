#include <stdio.h>
#include <stdlib.h>	/* exit(3) */
#include <unistd.h>	/* sysconf(3) */
#include <string.h>	/* memset(3) */

struct object{
	int src_cpu, dst_cpu;
	long pid;
	long sec, usec;
};

/*================================================================================
	ファイルを引数で指定して、解析し、CSVを出力するプログラム
	CSVはカレントディレクトリに出力される

	このプログラムはkprofiled::trace_lb_entryと協調して動作するので
	struct objectを変更した場合は同期させておくこと

	Usage:
	./lb_analysis "入力元(オブジェクトファイル名)"
==================================================================================*/

#define	NR_CPUS	8
#define	MAX_NR_HOLD	5

struct pid_loop{
	unsigned int loop;
	int src_cpu, dst_cpu;
};


/* たらい回し現象をCSVに出力 */
static void print_csv_float_on(FILE *f_csv, unsigned long long float_onld[])
{
	int i;

	fprintf(f_csv, "float_on\n\n");

	for(i = 0; i < MAX_NR_HOLD; i++){
		fprintf(f_csv, "%llu,", float_onld[i]);
	}

	fprintf(f_csv, "\n\n");
}

/* ピンポンタスク現象をCSVに出力 */
static void print_csv_round_trip(FILE *f_csv, unsigned long long map[][NR_CPUS])
{
	int i, j;

	fprintf(f_csv, "roud_trip\n\n");

	for(i = 0; i < NR_CPUS; i++){
		fprintf(f_csv, ",CPU%d", i);
	}
	fprintf(f_csv, "\n");

	for(i = 0; i < NR_CPUS; i++){
		fprintf(f_csv, "CPU%d,", i);

		for(j = 0; j < NR_CPUS; j++){
			fprintf(f_csv, "%llu,", map[i][j]);
			printf("%llu,", map[i][j]);
		}

		fprintf(f_csv, "\n");
		putchar('\n');
	}
	fprintf(f_csv, "\n");
}

/*
	objectファイルから配列にデータを取り込む関数 解析アルゴリズムのメイン
	@f_obj objectファイルのFILE構造体
	@round_trip ピンポンタスク現象のデータを格納する配列
	@float_on たらいまわし現象のデータを格納する配列

	return 読み込めたobjectの総数
*/
static int analyze_main(FILE *f_obj, unsigned long long round_trip[][NR_CPUS], unsigned long long float_on[])
{
	int counter = 0;
	struct object current, last;
	struct pid_loop pl_round_trip, pl_float_on;

	pl_round_trip.loop = 0;
	pl_float_on.loop = 0;

	fseek(f_obj, 0L, SEEK_SET);	/* 先頭までseek */

	while(fread(&current, sizeof(struct object), 1, f_obj) == 1){	/* struct objectが読み込める限りloop */
		if(last.pid == current.pid){
			if(last.src_cpu == current.dst_cpu && last.dst_cpu == current.src_cpu){	/* round-trip */

				if(pl_round_trip.loop == 0){		/* CPUをメモ */
					pl_round_trip.src_cpu = last.src_cpu;
					pl_round_trip.dst_cpu = last.dst_cpu;
				}
				pl_round_trip.loop++;
			}
			else{	/* たらい回し現象 */
				if(pl_float_on.loop == 0){		/* CPUをメモ */
					pl_float_on.src_cpu = last.src_cpu;
					pl_float_on.dst_cpu = last.dst_cpu;
				}
				pl_float_on.loop++;
			}
		}
		else{
			/* ピンポンタスク又は、たらい回し現象が終了したので、結果を配列に書き込む */
			if(pl_round_trip.loop != 0){
				round_trip[pl_round_trip.src_cpu][pl_round_trip.dst_cpu] += pl_round_trip.loop;	/* ループした数だけ増やしていく */
				pl_round_trip.loop = 0;
			}
			if(pl_float_on.loop != 0){
				float_on[pl_float_on.loop - 1]++;	/* 配列の添字は0から */
				pl_float_on.loop = 0;
			}
		}

		last = current;
		counter++;
	}

	return counter;
}

#define MAX_PATH_LEN	64

int main(int argc, char *argv[])
{
	int i, counter;
	char output_path[MAX_PATH_LEN];
	unsigned long long round_trip[NR_CPUS][NR_CPUS], float_on[MAX_NR_HOLD];
	FILE *f_csv = NULL, *f_obj = NULL;

	/* 格納用配列をクリア */
	memset(round_trip, 0, NR_CPUS * NR_CPUS * sizeof(unsigned long long));
	memset(float_on, 0, MAX_NR_HOLD * sizeof(unsigned long long));

	if(argc == 2){
		if((f_obj = fopen(argv[1], "rb")) == NULL){
			exit(EXIT_FAILURE);
		}
	}
	else{
		printf("引数でobjectファイルを指定してください\n");
		return 0;
	}

	sprintf(output_path, "./%s.csv", argv[1]);

	f_csv = fopen(output_path, "w");

	counter = analyze_main(f_obj, round_trip, float_on);

	fprintf(f_csv, "counter = %d\n\n", counter);

	puts("analyze_main() successfully done");

	print_csv_round_trip(f_csv, round_trip);
	puts("print_csv_round_trip() successfully done");

	print_csv_float_on(f_csv, float_on);
	puts("print_csv_float_on() successfully done");

	fclose(f_obj);
	fclose(f_csv);

	return 0;
}


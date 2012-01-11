#include <stdio.h>
#include <stdlib.h>	/* exit(3) */
#include <unistd.h>	/* sysconf(3) */
#include <string.h>	/* memset(3) */

#if 1
struct object{
	long sec, usec;
	unsigned long max_load_move, total_load_moved;
};
#endif

#if 0
struct object{
	unsigned long long min_cpu_load, max_cpu_load;
	long sec, usec;
};

#endif

#if 0
struct object{
	unsigned int iqr;
	int digit;
	long shifted_load;
	long sec, usec;
};
#endif

/*================================================================================
	オブジェクト列のファイルからCSVを書き出すプログラム
	CSVはカレントディレクトリに出力される

	このプログラムはkprofiled::trace_actually_loadと協調して動作するので
	struct objectを変更した場合は同期させておくこと

	./actually_load_analysis "入力元(オブジェクトファイル名)" 

==================================================================================*/


#define MAX_PATH_LEN	64

int main(int argc, char *argv[])
{
	int i, counter = 0;
	char output_path[MAX_PATH_LEN];
	FILE *f_obj, *f_csv;
	struct object obj;

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

	printf("sizeof(struct object):%d\n", sizeof(struct object));

	fprintf(f_csv, "max_load_move .vs total_load_moved\n\n");
	fprintf(f_csv, ",imbalance,total_load_moved");

	while(fread(&obj, sizeof(struct object), 1, f_obj) == 1){
		//printf("[%d.%d] min_cpu_load:%llu, max_cpu_load:%llu\n", obj.sec, obj.usec, obj.min_cpu_load, obj.max_cpu_load);
		//printf("[%d.%d] iqr:%d, digit:%d, shifted_load:%d\n", obj.sec, obj.usec, obj.iqr, obj.digit, obj.shifted_load);

		fprintf(f_csv, "%d.%d,%d,%d\n", obj.sec, obj.usec, obj.max_load_move, obj.total_load_moved);
		counter++;
	}

	putchar('\n');
	printf("総オブジェクト数：%d\n", counter);

	fclose(f_obj);
	fclose(f_csv);

	return 0;
}


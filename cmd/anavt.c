#include <stdio.h>
#include <stdlib.h>	/* exit(3) */
#include <unistd.h>	/* sysconf(3) */
#include <string.h>	/* memset(3) */

struct vtrecord {
	unsigned long long vruntime;
};

struct vtrecord_hdr{
	int nr_cpus;				/* システムの論理CPU数 */
	unsigned int nr_vtrecord;		/* 何個のエントリが記録されているか */
};

struct vtrecord_hdr hdr;
int nr_cpus;



int main(int argc, char *argv[])
{
	int i, counter;
	struct vtrecord vt;
	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	FILE *fvt = NULL;

	if(argc == 2){
		if((fvt = fopen(argv[1], "rb")) == NULL){
			exit(EXIT_FAILURE);
		}
	}
	else{
		printf("error:no specified file\n");
		return 0;
	}

	if(fread(&hdr, sizeof(struct vtrecord_hdr),1,fvt) != 1){
		puts("err");
	}

	while(fread(&vt, sizeof(struct vtrecord), 1, fvt) == 1){
		printf("vruntime:%llu\n", vt.vruntime);
	}

	printf("hdr->nr_cpus:%d, nr_vtrecord:%d\n", hdr.nr_cpus, hdr.nr_vtrecord);

	fclose(fvt);

	return 0;
}


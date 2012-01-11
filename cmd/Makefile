# cmd/Makefile

objs = lb_analysis actually_load


all: $(objs)

# コマンドの実行ファイル
lb_analysis: lb_analysis.c
	cc -o lb_analysis lb_analysis.c

actually_load: actually_load_analysis.c
	cc -o actually_load_analysis actually_load_analysis.c

clean:
	rm -f $(objs) 


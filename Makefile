# Makefile
CFLAGS = -DLOAD_KERNEL_BUILD
objs = kprofiled.o l3miss.o trace_lb_entry.o trace_actually_load.o kpreport.o
lib_path = ./lib
cmd_path = ./cmd

# kprofiled 関連のオブジェクトファイル
kprofiled: Makefile kprofiled.h $(objs) $(lib_path)/libkprofiled.a
	cc -Wall -o kprofiled $(objs) $(lib_path)/libkprofiled.a -lpthread

kprofiled.o: kprofiled.c
	cc -Wall -c kprofiled.c $(CFLAGS)

trace_lb_entry.o: trace_lb_entry.c
	cc -Wall -c trace_lb_entry.c $(CFLAGS)

trace_actually_load.o: trace_actually_load.c
	cc -Wall -c trace_actually_load.c $(CFLAGS)

kpreport.o: kpreport.c
	cc -Wall -c kpreport.c $(CFLAGS)

l3miss.o: l3miss.c
	cc -Wall -c l3miss.c $(CFLAGS)

$(lib_path)/libkprofiled.a:
	cd $(lib_path); make

# 各種操作
install: kprofiled kprofiled.sh
	cp kprofiled ./pkg-root/kprofiled/usr/bin/
	cp kprofiled.sh ./pkg-root/kprofiled/usr/share/kprofiled/

clean:
	rm -f kprofiled *.o *~

clean-all:
	rm -f kprofiled *.o
	cd $(cmd_path); make clean
	cd $(lib_path); make clean


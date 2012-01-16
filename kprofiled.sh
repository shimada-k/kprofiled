#!/bin/sh

#=========================================================================
#
#	kprofiled.sh
#	kprofiledと負荷プログラムと出力データの制御用スクリプト
#
#=========================================================================

KPROFILED_WD=/home/shimada/kprofiled/
CPUINFO=/proc/cpuinfo

TRACE_LB_ENTRY_DATA="$KPROFILED_WD"trace_lb_entry.data
TRACE_ACTUALLY_LOAD_DATA="$KPROFILED_WD"trace_actually_load.data
L3MISS_DATA="$KPROFILED_WD"l3miss.csv

NR_CPU=`cat "$CPUINFO" | grep processor | wc -l`

#データが格納されるディレクトリの名前は月.日付.時間（24H）
DATA_DIR="$KPROFILED_WD"data.`date +%m.%d.%H`/

BENCHMARK_TYPE="dummy"

#====================================================
#データファイルの移動、クリーンアップを行う関数
#引数を2つ受け取る
# $1:1CPUあたりのタスク数
# $2:sarのための引数
#====================================================
kprofiled_cleanup()
{
	#kprofiledを停止
	if [ -e "$KPROFILED_WD"kprofiled.pid ]
	then
		kill -15 `cat "$KPROFILED_WD"kprofiled.pid`

		#kprofiledが完全に停止するまで待つ
		sleep 10
	fi

	#データファイルをディレクトリに移動
	if [ $BENCHMARK_TYPE = "kbuild" ]
	then
		mv "$L3MISS_DATA"                  "$DATA_DIR""$1"_"$ELAPSE".l3miss.csv
		mv "$TRACE_LB_ENTRY_DATA"          "$DATA_DIR""$1"_"$ELAPSE".lb_entry
		mv "$TRACE_ACTUALLY_LOAD_DATA"     "$DATA_DIR""$1"_"$ELAPSE".actually_load
	else
		mv "$L3MISS_DATA"                  "$DATA_DIR""$START_T".l3miss.csv
		mv "$TRACE_LB_ENTRY_DATA"          "$DATA_DIR""$START_T".lb_entry
		mv "$TRACE_ACTUALLY_LOAD_DATA"     "$DATA_DIR""$START_T".actually_load
	fi

	#sarファイルから時間範囲指定してテキストファイルに出力する
	sar -A -s "$2" -e `date "+%T"` -f "$DATA_DIR"output.sar > "$DATA_DIR""$1"_"$ELAPSE".txt

	#次回のために掃除
	if [ $BENCHMARK_TYPE = "kbuild" ]
	then
		cd "$KERNEL_TREE"
		make clean
	fi
}

#==========================================
#kprofiledのメイン処理
#引数を一つ受け取る
# $1:並列度
#==========================================
kprofiled_main()
{
	START_T=`date "+%T"`

	# kprofiledを起動
	kprofiled workdir="$KPROFILED_WD"

	if [ $BENCHMARK_TYPE = "ab" ]
	then
		sleep 30

	elif [ $BENCHMARK_TYPE = "kbuild" ]
	then
		KERNEL_TREE=/home/shimada/linux-source-3.0.0/
		BUILD_T=`date "+%s"`
		NR_TASKS=`expr "$NR_CPU" \* "$1"`

		# ここからカーネルのビルド
		cd "$KERNEL_TREE"
		make -j"$NR_TASKS"
		BUILT_T=`date "+%s"`
		ELAPSE=`expr "$BUILT_T" - "$BUILD_T"`
	fi

	kprofiled_cleanup "$1" "$START_T"
}

#=============================================================
#	シェルスクリプト中で一回だけ行われる処理
#=============================================================

#引数解析
while getopts b: option
do
	case "$option" in
		b)
			if [ $OPTARG = "ab" -o $OPTARG = "kbuild" ]
			then
				BENCHMARK_TYPE=$OPTARG
				echo "$BENCHMARK_TYPEで負荷をかけます"
			else
				#引数が不正だったらエラーで終了
				echo "kprofiled.sh 引数指定が不正です"
				echo "Usage: #kprofiled -b [ab|kbuild]"
				exit 1
			fi
		;;
	esac
done


echo "DEBUG:$BENCHMARK_TYPE"

#データが格納されるディレクトリを作成
if [ ! -d "$DATA_DIR" ]
then
	mkdir "$DATA_DIR"
fi

# sarのためにロケールをCに変更
LANG=C

#sarで2秒ごとにシステム情報の取得開始（6400秒 == 3時間）
sar -P ALL 2 36000 -o "$DATA_DIR"output.sar > /dev/null &

#/dev/trace_lb_entryの有効化
MAJOR=$(awk "\$2==\"trace_lb_entry\" {print \$1}" /proc/devices)
echo "$MAJOR"
rm -f /dev/trace_lb_entry
mknod /dev/trace_lb_entry c "$MAJOR" 0

#/dev/trace_actually_loadの有効化
MAJOR=$(awk "\$2==\"trace_actually_load\" {print \$1}" /proc/devices)
echo "$MAJOR"
rm -f /dev/trace_actually_load
mknod /dev/trace_actually_load c "$MAJOR" 0

#l3missのためにmsrをロードしておく
modprobe msr

#=============================================================
#	メインの処理はここから
#=============================================================

#15から1づつseq(1)の第3引数までループ
#for i in `seq 32 2 34`
#do
#    kprofiled_main "$i"
#done

kprofiled_main 16
kprofiled_main 20


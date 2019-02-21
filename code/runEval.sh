#!/bin/bash

# ENCODE=./code/encode
ENCODE=./encode
# INPUTPATH=../eval/videos
INPUTPATH=.
# VIDEO=nyc_30s_3840x2048.yuv
VIDEO=out.yuv
WH="--width=3840 --height=2048"
# VIDEO=wingsuit.yuv
# WH="--width=1280 --height=720"


cd @CMAKE_CURRENT_BINARY_DIR@

rm -rf time filesize qualSize
mkdir -p time
mkdir -p filesize
mkdir -p qualSize

function doEncode() {
	echo "Rows: $1 Cols: $2"
	ROWS=$1
	COLS=$2
	COMMAND="${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/${ROWS}x${COLS}.hevc ${WH} --fps=25 --num-tile-rows=${ROWS} --num-tile-cols=${COLS} --tile-bitrates=10"
	echo ${COMMAND}
	/usr/bin/time --output=time/time-${ROWS}x${COLS}.txt ${COMMAND}
	echo ${ROWS}x${COLS} >> time/Sizes.txt
	wc -c time/${ROWS}x${COLS}.hevc >> time/Sizes.txt
}

if false;
then
    # <rows>x<columns>
    echo "Encode/stitch time vs. tiles"

    echo "3x8"
    echo ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/3x8.hevc ${WH} --fps=25 --num-tile-rows=3 --num-tile-cols=8 --tile-bitrates=10
    echo ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/3x8.hevc ${WH} --fps=25 --num-tile-rows=3 --num-tile-cols=8 --tile-bitrates=10

    echo "4x4"
    echo ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/4x4.hevc ${WH} --fps=25 --num-tile-rows=4 --num-tile-cols=4 --tile-bitrates=10
    time ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/4x4.hevc ${WH} --fps=25 --num-tile-rows=4 --num-tile-cols=4 --tile-bitrates=10

    echo "2x3"
    echo ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/2x6.hevc ${WH} --fps=25 --num-tile-rows=2 --num-tile-cols=6 --tile-bitrates=10
    ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/2x6.hevc ${WH} --fps=25 --num-tile-rows=2 --num-tile-cols=6 --tile-bitrates=10

    echo "2x3"
    echo ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/2x3.hevc ${WH} --fps=25 --num-tile-rows=2 --num-tile-cols=3 --tile-bitrates=10
    ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/2x3.hevc ${WH} --fps=25 --num-tile-rows=2 --num-tile-cols=3 --tile-bitrates=10

    echo "3x2"
    echo ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/3x2.hevc ${WH} --fps=25 --num-tile-rows=3 --num-tile-cols=2 --tile-bitrates=10
    ${ENCODE} --input=${INPUTPATH}/${VIDEO} --output=time/3x2.hevc ${WH} --fps=25 --num-tile-rows=3 --num-tile-cols=2 --tile-bitrates=10

else
    # for run in {1..10}
    for run in 1
    do
        # <rows>x<columns>
        echo "Encode/stitch time vs. tiles"
        echo "1x3"

	doEncode 1 1
	doEncode 1 2
	doEncode 1 3
	doEncode 1 4
	doEncode 1 5
	doEncode 1 6
	doEncode 1 7
	doEncode 1 8

	doEncode 2 1
	doEncode 2 2
	doEncode 2 3
	doEncode 2 4
	doEncode 2 5
	doEncode 2 6
	doEncode 2 7
	doEncode 2 8

	doEncode 3 1
	doEncode 3 2
	doEncode 3 3
	doEncode 3 4
	doEncode 3 5
	doEncode 3 6
	doEncode 3 7
	doEncode 3 8

	doEncode 4 1
	doEncode 4 2
	doEncode 4 3
	doEncode 4 4
	doEncode 4 5
	doEncode 4 6
	doEncode 4 7
	doEncode 4 8

	doEncode 5 1
	doEncode 5 2
	doEncode 5 3
	doEncode 5 4
	doEncode 5 5
	doEncode 5 6
	doEncode 5 7
	doEncode 5 8

	doEncode 6 1
	doEncode 6 2
	doEncode 6 3
	doEncode 6 4
	doEncode 6 5
	doEncode 6 6
	doEncode 6 7
	doEncode 6 8

	doEncode 7 1
	doEncode 7 2
	doEncode 7 3
	doEncode 7 4
	doEncode 7 5
	doEncode 7 6
	doEncode 7 7
	doEncode 7 8

	doEncode 8 1
	doEncode 8 2
	doEncode 8 3
	doEncode 8 4
	doEncode 8 5
	doEncode 8 6
	doEncode 8 7
	doEncode 8 8
    done
    grep user time/time-?x?.txt | sed -e 's/user.*//' -e 's/time.time-//' | sed -e 's/.txt:/ /' -e 's/x/ /' > Times.txt
    sed -e 'N;s/\n/ /' -e 's/x/ /' -e 's/ time.*//' time/Sizes.txt > Sizes.txt
fi


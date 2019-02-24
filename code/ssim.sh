#!/bin/bash

# Usage:
#
# If the videos must first be converted to PNGs:
#   $ ./ssim.sh --generate-pngs refvideo.mp4 testvideo.mp4
#
# If the PNG folders already exist:
#   $ ./ssim.sh

REF_DIR='./ref_pngs'
TEST_DIR='./test_pngs'
NUM_DECIMALS=7

if [[ $* == *--generate-pngs* ]]; then
	REF_VIDEO=$2
	TEST_VIDEO=$3
	rm -rf $REF_DIR
	rm -rf $TEST_DIR
	mkdir $REF_DIR
	mkdir $TEST_DIR
	ffmpeg -i $REF_VIDEO $REF_DIR/frame%04d.png
	ffmpeg -i $TEST_VIDEO $TEST_DIR/frame%04d.png
fi

NUM_FRAMES=$(ls $REF_DIR/*.png -1 | wc -l)
ssimTotal=0

for frame in $REF_DIR/*.png; do
	imgName=$(basename $frame)
	ssimFrame=$(pyssim $REF_DIR/$imgName $TEST_DIR/$imgName)
	ssimTotal=$(echo "scale=$NUM_DECIMALS; $ssimTotal + $ssimFrame" | bc)
done

echo "scale=$NUM_DECIMALS; $ssimTotal / $NUM_FRAMES" | bc

#!/bin/bash

for run in {1..10}
do
    # <rows>x<columns>
    echo "Encode/stitch time vs. tiles"
    echo "1x3"
    time ../code/encode --input=videos/nyc_30s_3840x2048.yuv --output=time/1x3.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=1 --num-tile-cols=3 --tile-bitrates=101
    echo "3x1"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=time/3x1.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=3 --num-tile-cols=1 --tile-bitrates=101
    echo "3x3"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=time/3x3.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=3 --num-tile-cols=3 --tile-bitrates=101010101
    echo "5x5"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=time/5x5.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=5 --num-tile-cols=5 --tile-bitrates=1010101010101010101010101
    echo "9x9"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=time/9x9.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=9 --num-tile-cols=9 --tile-bitrates="$(printf '10%.0s' {1..40})1"

    echo "Output filesize vs. tiles"
    echo "1x2"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=filesize/1x2.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=1 --num-tile-cols=2 --tile-bitrates=10
    echo "2x1"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=filesize/2x1.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=2 --num-tile-cols=1 --tile-bitrates=10
    echo "2x2"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=filesize/2x2.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=2 --num-tile-cols=2 --tile-bitrates=1010
    echo "4x4"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=filesize/4x4.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=4 --num-tile-cols=4 --tile-bitrates=1010101010101010
    echo "8x8"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=filesize/8x8.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=8 --num-tile-cols=8 --tile-bitrates="$(printf '10%.0s' {1..32})"

    echo "All high tiles vs. all low tiles"
    echo "3x3 for both"
    echo "High"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=qualSize/3x3_high.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=3 --num-tile-cols=3 --tile-bitrates=000000000
    echo "Low"
    time ../code/encode --input=../eval/videos/nyc_30s_3840x2048.yuv --output=qualSize/3x3_low.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=3 --num-tile-cols=3 --tile-bitrates=111111111
done

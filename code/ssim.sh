#!/bin/bash

REF_VIDEO="out.yuv"
RESOLUTION="3840x2048"
FF_RESOLUTION=$(echo -e "width=3840\nheight=2048")

# First arg: directory
# Second arg: quality (to label the results afterwards)
IFS=
get_dir_ssim_scores () {
	TEST_DIR=$1
	QUAL=$2
	echo "Quality config: $QUAL"
	for videoPath in $TEST_DIR/*.hevc; do
		video=$(basename $videoPath)
        tileConfig=$(basename $video .hevc)
        echo "$video"

        vidSize=$(ffprobe -v error -show_entries stream=width,height -of default=noprint_wrappers=1 $videoPath)
        echo "$vidSize"

        if [[ "$vidSize" == "$FF_RESOLUTION" ]]; then
	    echo ""
    	else
            ffmpeg -loglevel panic -y -i $videoPath -vf scale=3840:2048 resized.hevc
	    videoPath="./resized.hevc"
        fi
	result=$(ffmpeg -i $videoPath -s:v $RESOLUTION -i $REF_VIDEO -lavfi "ssim" -f null - 2>&1)
	echo $result
	echo $result | grep Parsed_ssim | awk -v tileConfig="$tileConfig" -v qualConfig="$QUAL" '{print qualConfig "," tileConfig "," substr($8, 5)}' >> ssim_eval.csv
	done
}

# Get scores for each quality configuration
get_dir_ssim_scores "time_high" "high"
get_dir_ssim_scores "time_low" "low"
get_dir_ssim_scores "time_mixed" "mixed"


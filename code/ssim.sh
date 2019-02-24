#!/bin/bash

REF_VIDEO="out.yuv"
RESOLUTION="3840x2048"

# First arg: directory
# Second arg: quality (to label the results afterwards)
get_dir_ssim_scores () {
	TEST_DIR=$1
	QUAL=$2
	echo "Quality config: $QUAL"
	for videoPath in $TEST_DIR/*.hevc; do
		video=$(basename $videoPath)
        tileConfig=$(basename $video .hevc)
        echo "$video"

        vidSize=$(ffprobe -v error -show_entries stream=width,height -of default=noprint_wrappers=1 $video)
        echo "$vidSize"
        
        if [[ $vidSize == "test" ]]; then
            echo "what"
        fi
		#ffmpeg -i $videoPath -s:v $RESOLUTION -i $REF_VIDEO -lavfi "ssim" -f null - 2>&1 | grep Parsed_ssim | awk -v tileConfig="$tileConfig" -v qualConfig="$QUAL" '{print qualConfig "," tileConfig "," substr($8, 5)}' >> ssim_eval.csv
	done
}

# Get scores for each quality configuration
get_dir_ssim_scores "time_high" "high"
get_dir_ssim_scores "time_low" "low"
get_dir_ssim_scores "time_mixed" "mixed"

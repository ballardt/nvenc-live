#!/bin/bash

# Non-debug
g++ -static -c -o stitch.o stitch.cpp
gcc -static -c -o encode.o nvenc_encode.c -I/home/tballard/ffmpeg_build/include -L/home/tballard/ffmpeg_build/lib -lswscale -lavdevice -lavformat -lavcodec -lavutil -lswresample -fvisibility=hidden
g++ -o encode encode.o stitch.o -I/home/tballard/ffmpeg_build/include -L/home/tballard/ffmpeg_build/lib -lswscale -lavdevice -lavformat -lavcodec -lavutil -lswresample -fvisibility=hidden
./encode --input=../../videos/nyc_30s.yuv --output=stitched_ms9390.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=2 --num-tile-cols=3 --tile-bitrates=101010

# Debug
###g++ -static -g -O0 -c -o stitch.o stitch.cpp
###gcc -static -g -O0 -c -o encode.o nvenc_encode.c -I/home/tballard/ffmpeg_build/include -L/home/tballard/ffmpeg_build/lib -lswscale -lavdevice -lavformat -lavcodec -lavutil -lswresample -fvisibility=hidden
###g++ -g -O0 -o encode encode.o stitch.o -I/home/tballard/ffmpeg_build/include -L/home/tballard/ffmpeg_build/lib -lswscale -lavdevice -lavformat -lavcodec -lavutil -lswresample -fvisibility=hidden
###gdb --args ./encode --input=../../videos/nyc_30s.yuv --output=stitched_ms9390.hevc --width=3840 --height=2048 --fps=25 --num-tile-rows=2 --num-tile-cols=3 --tile-bitrates-file=tbs.csv

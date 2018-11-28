cmake_minimum_required(VERSION 3.0)

project(nvenc-live VERSION 0.1)

#
# Make sure that you use the right version of libav when calling cmake,
# for example:
#   cmake -DLIBAV_ROOT_DIR=/home/tballard/ffmpeg_build/ ..
#

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(LibAV COMPONENTS avcodec avutil swresample)

add_subdirectory(code)

configure_file(runEval.sh.in runEval.sh @ONLY)

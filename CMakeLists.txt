cmake_minimum_required(VERSION 3.0)

project(nvenc-live VERSION 0.1)

#
# Make sure that you use the right version of libav when calling cmake,
# for example:
# cmake -DBOOST_ROOT=/home/griff/Install/boost -DLIBAV_ROOT_DIR=/home/griff/Install/ffmpeg-nvenc ..
#

set(CMAKE_CXX_STANDARD 11)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")	

# ==============================================================================
# Boost
# ==============================================================================
# some possible boost components:
# atomic date_time filesystem log regex serialization
find_package(Boost 1.60.0 QUIET COMPONENTS container program_options system thread)

if(Boost_FOUND)
    message(STATUS "Boost ${Boost_LIB_VERSION} found.")
else()
    message(STATUS "Failed to find Boost.")
    message(FATAL_ERROR "${Boost_ERROR_REASON}")
endif()

# Disable BOOST autolink
add_definitions(-DBOOST_ALL_NO_LIB)

# Force BOOST to use dynamic libraries (avoid link error with boost program_options)
# https://lists.boost.org/boost-users/2009/11/54015.php
add_definitions(-DBOOST_ALL_DYN_LINK)

link_directories(${Boost_LIBRARY_DIRS})
add_definitions(${Boost_DEFINITIONS})
# ==============================================================================

# ==============================================================================
# ffmpeg/libav
# ==============================================================================
find_package(LibAV COMPONENTS swscale avdevice avformat avcodec avutil)
if(LIBAV_SWSCALE_FOUND)
    message(STATUS "LibAV ${LIBAV_VERSION} found.")
    message(STATUS "LibAV include dirs: ${LIBAV_INCLUDE_DIRS}")
else()
    message(FATAL_ERROR "Failed to find ffmpeg/libav.")
endif()
# ==============================================================================

add_subdirectory(code)


# Introduction

RATS was built using Ubuntu 17 and a GTX 1080Ti. Any recent Ubuntu distro and a GPU with NVENCODE should work, but may require changes to the installation process. The goal of RATS is to encode a raw video file (YUV420p) into an HEVC bitstream in real-time in such a way that different parts of the video are encoded at different bitrates. This is accomplished by rearranging the pixels in the source then encoding each frame twice--once at a high bitrate, and once at a low one. Finally, the resulting bitstreams are stitched together into a single image containing some high-bitrate parts and some low-bitrate parts, and the pieces that aren’t used in the final image are simply discarded.


This document will focus on the implementation of RATS and its usage. For a more thorough discussion on the idea behind RATS, see the demo paper. For any questions or comments, please contact trevorcoleballard@gmail.com.

# Installation

## NVENC and ffmpeg

ffmpeg’s libraries are used to interact with NVENC, and both must be compiled and set up in a specific way. Also note that you must use the RATS fork of ffmpeg (https://github.com/ballardt/ffmpeg), as this version changes the sliceMode and sliceModeData used for encoding, which is necessary to obtain the desired tiling behavior.

Note that these commands worked for me, but I did not use the GPU for anything else. I have not tested them on other systems, or for other use cases. The system used is Ubuntu 17, with a GTX 1080 Ti.

### Installing the CUDA SDK

In the terminal, enter:

    for FILE in $(dpkg-divert --list | grep nvidia-340 | awk '{print $3}'); do dpkg-divert --remove $FILE; done
    apt --fix-broken install
    sudo apt install -f nvidia-kernel-source-396
    sudo apt install -f nvidia-driver-396
    mkdir ~/ffmpeg_sources
    mkdir ~/bin
    sudo apt-get -y install autoconf automake build-essential libass-dev \
      libtool pkg-config texinfo zlib1g-dev cmake mercurial
    cd ~/ffmpeg_sources
    wget -c -v -nc https://developer.nvidia.com/compute/cuda/9.2/Prod2/local_installers/cuda-repo-ubuntu1710-9-2-local_9.2.148-1_amd64
    mv cuda-repo-ubuntu1710-9-2-local_9.2.148-1_amd64 cuda-repo-ubuntu1710-9-2-local_9.2.148-1_amd64.deb
    sudo dpkg -i cuda-repo-ubuntu1710-9-2-local_9.2.148-1_amd64.deb
    sudo apt-key add /var/cuda-repo-9-2-local/7fa2af80.pub
    sudo apt-get update
    sudo apt-get install cuda

If you encounter any issues involving “dpkg: error processing (...)”, run

    sudo dpkg -i --force-overwrite /var/cache/apt/archives/<package>.deb
     sudo apt-get -f install
on the problematic packages until success.

Once finished, run

    sudo ldconfig

Next, edit /etc/environment and append “CUDA\_HOME=/usr/local/cuda” to the end of the file. Then add “/usr/local/cuda/bin:$HOME/bin” to PATH.

Finally, reboot the computer. Once rebooted, proceed to install ffmpeg.

### FFmpeg

This is similar to the official installation guide, but some packages are excluded or slightly changed, and the configure options at the very end are different in order to use NVENC. 

First, clone the RATS fork of FFmpeg and create a directory for the build:

    git clone https://github.com/ballardt/ffmpeg/ ~/ffmpeg_sources
    mkdir ~/ffmpeg_build

FFmpeg is installed via a few different modules, which we will install in turn. Note that, at the time of this writing, there was an upstream issue with using libfdk-aac with FFmpeg. Audio is not important to RATS, so we will not install libfdk-aac.

First, install NASM 2.14:

    cd ~/ffmpeg_sources
    wget http://www.nasm.us/pub/nasm/releasebuilds/2.14rc0/nasm-2.14rc0.tar.gz
    tar xzvf nasm-2.14rc0.tar.gz
    cd nasm-2.14rc0 
    ./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin" 
    make -j$(nproc) VERBOSE=1 
    make -j$(nproc) install 
    make -j$(nproc) distclean

Next, install libx264:

    cd ~/ffmpeg_sources
    git clone http://git.videolan.org/git/x264.git -b stable
    cd x264/
    PATH="$HOME/bin:$PATH" ./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin" --enable-static --enable-shared --disable-opencl
    PATH="$HOME/bin:$PATH" make -j$(nproc) VERBOSE=1
    make -j$(nproc) install
    make -j$(nproc) distclean

Next, install libx265:

    cd ~/ffmpeg_sources 
    hg clone https://bitbucket.org/multicoreware/x265 
    cd ~/ffmpeg_sources/x265/build/linux 
    PATH="$HOME/bin:$PATH" cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="$HOME/ffmpeg_build" -DENABLE_SHARED:bool=off ../../source 
    make -j$(nproc) VERBOSE=1 
    make -j$(nproc) install 
    make -j$(nproc) clean

Next, install FFmpeg:

    cd ~/ffmpeg_sources 
    cd FFmpeg 
    CUDA_HOME="/usr/local/cuda" 
    PATH="/usr/local/cuda/bin:$HOME/bin:$PATH" 
    PATH="$HOME/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure \ 
     --prefix="$HOME/ffmpeg_build" \ 
     --pkg-config-flags="--static" \ 
     --extra-ldflags="-L$HOME/ffmpeg_build/lib" \ 
     --extra-cflags="-I$HOME/ffmpeg_build/include" \ 
     --bindir="$HOME/bin" \ 
     --incdir="$HOME/ffmpeg_build/include" \ 
     --enable-cuda-sdk \ 
     --enable-cuvid \ 
     --enable-libnpp \ 
     --extra-cflags="-I/usr/local/cuda/include/" \ 
     --extra-ldflags=-L/usr/local/cuda/lib64/ \ 
     --nvccflags="-gencode arch=compute_61,code=sm_61 -O2" \ 
     --enable-gpl \ 
     --enable-libass \ 
     --enable-libx264 \ 
     --extra-libs=-lpthread \ 
     --enable-libx265 \ 
     --enable-nvenc \ 
     --enable-nonfree \ 
     --enable-shared 
    PATH="$HOME/bin:$PATH" make -j$(nproc) VERBOSE=1 
    sudo make -j$(nproc) install 
    make -j$(nproc) distclean 
    hash -r 
    sudo ldconfig 

> You may experience an issue during configuration regarding a missing ffnvenc. If so, run the commands below
>
> ```
> git clone https://git.videolan.org/git/ffmpeg/nv-codec-headers.git
> cd nv-codec-headers
> make
> sudo make install
> ```

Next, edit /etc/ld.so.conf and append “/home/<your username>/ffmpeg\_build/lib” to the bottom of it. Now run 

    sudo ldconfig 

Finally, install nvidia-patch to remove the limits imposed by NVIDIA on consumer-grade GPUs. Follow the instructions at [https://github.com/keylase/nvidia-patch](https://github.com/keylase/nvidia-patch) to ensure your NVIDIA driver version is supported. The instructions are nearly the same for all versions, substituting only the version number. For example, if you are using version 396.54:

    mkdir /opt/nvidia && cd /opt/nvidia
    wget https://download.nvidia.com/XFree86/Linux-x86_64/396.54/NVIDIA-Linux-x86_64-396.54.run
    chmod +x ./NVIDIA-Linux-x86_64-396.54.run
    ./NVIDIA-Linux-x86_64-396.54.run
	git clone https://github.com/keylase/nvidia-patch
	cd nvidia-patch
	./patch.sh

You should now be ready to build RATS.

## RATS

Move to the RATS source code directory (code/). Then:

    mkdir build
    cd build
    cmake ..
    make
    make install

# Usage

TODO: update if necessary

Execute the "encode" object from the last step of the build with the required parameters. The parameters are as follows:

|CLI Parameter |Description |Required |
|--------------|------------|---------|
|--input |The input file, a YUV420p video. |Yes |
|--output |The filename of the final HEVC bitstream. |Yes |
|--width |The width of the video in pixels. Assumes input and output width are identical. |Yes |
|--height |The height of the video in pixels. Assumes input and output height are identical. |Yes |
|--fps |The speed of the video in frames-per-second. |Yes |
|--high-bitrate |The bitrate to encode high-quality tiles at. |No (default: 1600000) |
|--low-bitrate |The bitrate to encode low-quality tiles at. |No (default: 800000) |
|--num-tile-rows |The number of rows of tiles. |Yes |
|--num-tile-cols |The number of columns of tiles. |Yes |
|--tile-bitrates |A string of 1s and 0s dictating the quality of each tile, beginning at the top left and proceeding down, then wrapping to the next column at the bottom of the tile column. A 1 is low-quality, while a 0 is high quality. Must match the number of tiles in image. |Yes |

For example, say you have a 3840x2048 video that you want to cut into 6 tiles: 2 tile rows, 3 tile columns. The video is 30fps, and you want the middle tile column to be high bitrate, and the outer columns to be low bitrate. Furthermore, you want to use the default low and high bitrate values. The command would then be:

    # chmod must be called once, after the encode object is first created
    sudo chmod +x /path/to/encode
    ./encode --input=input_video.yuv --output=output_video.hevc --width=3840 --height=2048 --fps=30 --num-tile-rows=2 --num-tile-cols=3 --tile-bitrates=001100

## Videos

A good collection of test videos are found in the "360-Degree Videos Head Movements Dataset" at http://dash.ipv6.enstb.fr/headMovements/. Use FFmpeg to trim the videos to a short length (e.g. 5s or 30s), then used FFmpeg to convert them to YUV420p files. Note that YUV420p is simply raw images one after another, so the file sizes can be extremely large. This is why one should first trim the video to a short length.

To trim a video to a certain length, specify the start time with `-ss` and duration with `-t`. For example, to trim a video, starting 10s into the video, to a 30s video:

    ffmpeg -i input_video.mp4 -ss 00:00:10 -t 00:00:30 -async 1 -strict -2 trimmed_video.mp4

To convert to YUV, follow the below. Note that the `-r` option is the FPS, `-s:v` is the resolution, and `-vcodec` need not be `hevc_nvenc`, though it may take longer otherwise. Also, do not leave the curly braces; if you don't want to specify, e.g., a preset, just remove that whole thing:

    ffmpeg -i input_vid.mp4 -c:v rawvideo -pix_fmt yuv420p output_vid.yuv


# System Overview

TODO update for new C++ code

There are two primary source files, nvenc\_encode.c and stitch.cpp. Nvenc\_encode.c is the main file, with stitch.cpp being called only to do the stitching. The whole pipeline repeats for each frame in the source YUV420p video, and consists of the following steps:


1. Read the next frame from the source file (nvenc\_encode.c)
2. Rearrange the pixels in the frame (nvenc\_encode.c)
3. Send the rearranged frame to NVENC (nvenc\_encode.c)
4. Get the bitstreams from NVENC (nvenc\_encode.c)
5. Perform stitching to get the final frame bitstream (stitch.cpp)
6. Write the frame bitstream to a file (nvenc\_encode.c)


We mix C and C++ because interactions with NVENC are handled via the ffmpeg APIs. This simplifies the process of using NVENC greatly, but these libraries are written in C. C++ is used for the stitching because the bitstream is manipulated via the Boost dynamic\_bitset API, which is one of the few libraries available to modify bitstreams, and the easiest to understand. Interaction between these two is handled via a shared header file, link\_stitcher.h, in which functions and variables common to both can be listed in an extern “C” block. Object files are then generated for either source file, which are linked by the compiler at the end to get the executable.

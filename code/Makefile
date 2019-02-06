all: encode

encode: nvenc_hw.o nvenc_encode.o nvenc_config.o stitch.o
	g++ -g -o encode \
		nvenc_hw.o nvenc_encode.o nvenc_config.o stitch.o \
		-I/home/griff/Install/ffmpeg-nvenc/include \
		-L/home/griff/Install/ffmpeg-nvenc/lib \
		-Wl,-rpath=/home/griff/Install/ffmpeg-nvenc/lib \
		-lswscale -lavdevice -lavformat \
		-lavcodec -lavutil -lswresample \
		-fvisibility=hidden

stitch.o: stitch.cpp
	gcc -g -static \
		-std=c++11 \
		-I/home/griff/Install/boost/include \
		-c -o stitch.o stitch.cpp 

nvenc_encode.o: nvenc_encode.cpp
	g++ -g -static -c -o nvenc_encode.o nvenc_encode.cpp \
		-Wno-deprecated-declarations \
		-std=c++11 \
		-I/home/griff/Install/boost/include \
		-I/home/griff/Install/ffmpeg-nvenc/include \
		-fvisibility=hidden

nvenc_hw.o: nvenc_hw.cpp
	g++ -g -static -c -o nvenc_hw.o nvenc_hw.cpp \
		-Wno-deprecated-declarations \
		-std=c++11 \
		-I/home/griff/Install/boost/include \
		-I/home/griff/Install/ffmpeg-nvenc/include \
		-fvisibility=hidden

nvenc_config.o: nvenc_config.cpp
	g++ -g -static -c -o nvenc_config.o nvenc_config.cpp \
		-I/home/griff/Install/boost/include \
		-fvisibility=hidden

clean:
	rm -f encode *.o


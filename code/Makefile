all: encode

encode: encode.o stitch.o
	g++ -g -o encode encode.o stitch.o \
		-I/home/griff/Install/ffmpeg-nvenc/include \
		-L/home/griff/Install/ffmpeg-nvenc/lib \
		-Wl,-rpath=/home/griff/Install/ffmpeg-nvenc/lib \
		-lswscale -lavdevice -lavformat \
		-lavcodec -lavutil -lswresample \
		-fvisibility=hidden

stitch.o: stitch.cpp
	gcc -g -static \
		-I/home/griff/Install/boost/include \
		-c -o stitch.o stitch.cpp 

encode.o: nvenc_encode.cpp
	g++ -g -static -c -o encode.o nvenc_encode.cpp \
		-I/home/griff/Install/boost/include \
		-I/home/griff/Install/ffmpeg-nvenc/include \
		-fvisibility=hidden

clean:
	rm -f encode encode.o stitch.o


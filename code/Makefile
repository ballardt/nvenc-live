INCLUDES=-I/home/griff/Install/boost/include \
         -I/home/griff/Install/ffmpeg-nvenc/include

CXXFLAGS=-g \
         -Wno-deprecated-declarations \
         -std=c++11 \
	 $(INCLUDES)

LDFLAGS= -g \
	 -L/home/griff/Install/ffmpeg-nvenc/lib \
	 -Wl,-rpath=/home/griff/Install/ffmpeg-nvenc/lib

LIBRARIES=-lswscale -lavdevice -lavformat \
          -lavcodec -lavutil -lswresample

all: encode

encode: nvenc_hw.o nvenc_encode.o nvenc_config.o stitch.o context_group.o
	g++ $(LDFLAGS) -o encode $^ $(LIBRARIES)

%.o : %.cpp
	g++ $(CXXFLAGS) -c $< -o $@

clean:
	rm -f encode *.o


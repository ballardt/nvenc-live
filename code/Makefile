INCLUDES=-I/home/griff/Install/boost/include \
         -I/home/griff/Install/ffmpeg-nvenc/include

CXXFLAGS=-O3 \
         -Wno-deprecated-declarations \
         -std=c++11 \
	 $(INCLUDES)

LDFLAGS= -O3 \
	 -L/home/griff/Install/ffmpeg-nvenc/lib \
	 -Wl,-rpath=/home/griff/Install/ffmpeg-nvenc/lib

LIBRARIES=-lswscale -lavdevice -lavformat \
          -lavcodec -lavutil -lswresample

all: encode

encode: nvenc_hw.o nvenc_encode.o nvenc_config.o stitch.o context_group.o
	g++ $(LDFLAGS) -o encode $^ $(LIBRARIES)

%.o : %.cpp
	g++ $(CXXFLAGS) -c $< -o $@

ssimplot: ssim_eval.csv
	sed -e 's/,/ /g' < ssim_eval.csv > /tmp/UML1.txt
	awk '{printf("%s %s %s\n",$$2,$$1,$$3);}' < /tmp/UML1.txt | sort > /tmp/UML2.txt
	paste -d " " - - - < /tmp/UML2.txt > /tmp/UML3.txt
	awk '{printf("%d %s\n",NR,$$0);}' < /tmp/UML3.txt > /tmp/UML4.txt
	awk '/x7/{next;} /x8/{next;} {print $0;}' < /tmp/UML4.txt > /tmp/UML5.txt
	gnuplot ssim.plot

clean:
	rm -f encode *.o


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "libavutil/common.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"

#include "link_stitcher.h"

#define NUM_SPLITS 3 // TODO replace with numTileCols; they're equivalent
#define BITSTREAM_SIZE 100000

static AVBufferRef* hwDeviceCtx = NULL;
static enum AVPixelFormat hwPixFmt;
static int hardwareInitialized = 0;
const char* codecName;
const AVCodec* codec;
AVCodecContext* codecContextArr[] = {NULL, NULL};
AVFrame* frames[] = {NULL, NULL};
AVPacket* pkt;
enum AVHWDeviceType hwDeviceType;

typedef enum bitrate {
	HIGH_BITRATE = 0,
	LOW_BITRATE
} Bitrate;
int bitrateValues[2];

typedef struct {
	char* inputFilename;
	char* outputFilename;
	int width;
	int height;
	double fps; // TODO
	int highBitrate;
	int lowBitrate;
	int numTileCols; // TODO
	int numTileRows; // TODO
	char* tileBitratesFilename; // TODO
} Config;

/**
 * Get the next frame, consisting of a Y, U, and V component.
 * Returns 1 if a frame was available, or 0 if there are no frames left in the file
 */
int getNextFrame(FILE* file, unsigned char* y, unsigned char* u, unsigned char* v, int ySize) {
	int uvSize = ySize / 4;
	int yRes, uRes, vRes;
	if (fread(y, sizeof(unsigned char), ySize, file) != ySize ||
		fread(u, sizeof(unsigned char), uvSize, file) != uvSize ||
		fread(v, sizeof(unsigned char), uvSize, file) != uvSize) {
		return 0;
	}
	return 1;
}

/**
 * Cut a frame component (Y, U, or V) into thirds and stack them on top of each other.
 */
void rearrangeFrameComponent(unsigned char** component, int origWidth, int origHeight,
							 int numSplits) {
	int oldIdx, newIdx;
	int newWidth = origWidth / numSplits;
	unsigned char* rearranged = (unsigned char*)malloc(sizeof(unsigned char) * origWidth * origHeight);
	for (int i=0; i<numSplits; i++) {
		for (int j=0; j<origHeight; j++) {
			oldIdx = (i*newWidth) + (j*origWidth);
			newIdx = (j*newWidth) + (i*origHeight*newWidth);
			memcpy(rearranged+newIdx, (*component)+oldIdx, newWidth); 
		}
	}

	unsigned char* temp = *component;
	*component = rearranged;
	free(temp);
}

/**
 * Cut a frame into thirds and stack them on top of each other.
 */
void rearrangeFrame(unsigned char** yPtr, unsigned char** uPtr, unsigned char** vPtr, int yWidth,
					int yHeight) {
	int uvWidth = yWidth / 2;
	int uvHeight = yHeight / 2;
	rearrangeFrameComponent(yPtr, yWidth, yHeight, NUM_SPLITS);
	rearrangeFrameComponent(uPtr, uvWidth, uvHeight, NUM_SPLITS);
	rearrangeFrameComponent(vPtr, uvWidth, uvHeight, NUM_SPLITS);
}

/**
 * Initialize the hardware context. This should be the very first thing to happen.
 */
void initializeHardware() {
	av_log_set_level(40);
	// Load the hardware
	hwDeviceType = av_hwdevice_find_type_by_name("cuda");
	if (hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
		printf("Device type cuda is not supported.\n");
		printf("Available device types:");
		while((hwDeviceType = av_hwdevice_iterate_types(hwDeviceType)) != AV_HWDEVICE_TYPE_NONE) {
			printf(" %s", av_hwdevice_get_type_name(hwDeviceType));
		}
		printf("\n");
		exit(1);
	}
	// Load the codec
	avcodec_register_all();
	codecName = "hevc_nvenc";
	codec = avcodec_find_encoder_by_name(codecName);
	if (!codec) {
		printf("Codec hevc_nvenc not found\n");
		exit(1);
	}
	// Create the hardware context
	int result;
	result = av_hwdevice_ctx_create(&hwDeviceCtx, hwDeviceType, NULL, NULL, 0);
	if (result < 0) {
		printf("Failed to create HW device\n");
		exit(1);
	}
}

/**
 * This does not get called directly, 
 */
static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hwPixFmt)
            return *p;
    }
	printf("Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

/**
 * Initialize a context object for a given bitrate. This should occur after the hardware 
 * initialization.
 */
void initializeContext(Bitrate bitrate, int width, int height) {
	// Allocate codec context. This is the struct with all of the parameters that
	// will be used when running the encoder, like bit rate, width/height, and GOP
	int ret;
	AVCodecContext* c = avcodec_alloc_context3(codec);
	if (!c) {
		printf("Error allocating video codec context\n");
		exit(1);
	}
	// Allocate space for the packet. NVENC will deposit the HEVC bitstream into pkt.
	// To get the video, we just append all of the pkt's together.
	pkt = av_packet_alloc();
	if (!pkt) {
		printf("Error allocating packet\n");
		exit(1);
	}
	// Allocate space for the frame. This is a struct representing one frame of the
	// raw video. These are sent directly to NVENC.
	AVFrame* frame = av_frame_alloc();
	if (!frame) {
		printf("Error allocating frame\n");
		exit(1);
	}
	// Initialize GPU encoder
	c->get_format = getHwFormat;
	av_opt_set_int(c, "refcounted_frames", 1, 0);
	c->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

	// Encoding parameters
	// We multiply width by 2 because we encode two full videos, but the tile width will
	// only be the left or right half of the full video.
	//c->bit_rate = 400000; // TODO what is the equivalent for kvazaar?
	c->width = width;
	c->height = height;
	// FPS
	// TODO this is not giving us good timing values
	c->time_base = (AVRational){1, 25};
	// Image specifications
	//c->gop_size = state->encoder_control->cfg.gop_len; // TODO this comes up as only 4?
	c->gop_size = 16;
	c->pix_fmt = AV_PIX_FMT_YUV420P;
	// Whether high or low bitrate
	c->bit_rate = bitrateValues[bitrate];

	// Open the codec
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		printf("Error opening codec\n");
		exit(1);
	}
	// Open the frame
	frame->format = c->pix_fmt;
	frame->width = c->width;
	frame->height = c->height;
	ret = av_frame_get_buffer(frame, 32);
	if (ret < 0) {
		printf("Error allocating frame data\n");
		exit(1);
	}
	codecContextArr[bitrate] = c;
	frames[bitrate] = frame;
}

int sendFrameToNVENC(Bitrate bitrate, AVFrame* frame, unsigned char* bitstream) {
	int bsPos = 0;
	pkt->data = NULL;
	pkt->size = 0;
	// Send the frame
	int ret;
	if ((ret = avcodec_send_frame(codecContextArr[bitrate], frame)) < 0) {
		printf("Error sending frame for encoding\n");
		exit(1);
	}
	// Try to receive packets until there are none
	while (ret >= 0) {
		ret = avcodec_receive_packet(codecContextArr[bitrate], pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			// Note: we always return here
			memcpy(bitstream+bsPos, pkt->data, pkt->size);
			int frameSize = bsPos + pkt->size;
			if (frameSize > BITSTREAM_SIZE) {
				printf("ERROR: frameSize > BITSTREAM_SIZE (%d > %d)\n", frameSize, BITSTREAM_SIZE);
			}
			//bsPos += pktSize;
			//fwrite(pkt->data, 1, pkt->size, file);
			av_packet_unref(pkt);
			return frameSize;
		}
		else if (ret < 0) {
			printf("Error during encoding\n");
			exit(1);
		}
		// Get the data from the GPU
		// Note: this never gets executed...
		memcpy(bitstream+bsPos, pkt->data, pkt->size);
		bsPos += pkt->size;
		//fwrite(pkt->data, 1, pkt->size, file);
		av_packet_unref(pkt);
	}
}

/**
 * Encode a frame with a given AVContext. In other words, encode either at a high
 * or low quality.
 */
void encodeFrameWithContext(unsigned char* bitstream, unsigned char* y, unsigned char* u,
							unsigned char* v, int yWidth, int yHeight, Bitrate bitrate,
							int* bitstreamSize) {
	if (codecContextArr[bitrate] == NULL) {
		initializeContext(bitrate, yWidth, yHeight);
	}
	int ret;
	AVFrame* frame = frames[bitrate];
	ret = av_frame_make_writable(frame);
	if (ret < 0) {
		printf("Frame not writable\n");
		exit(1);
	}
	// Copy the source image to the frame to be encoded
	int uvWidth = yWidth / 2;
	for (int i=0; i<yHeight; i++) {
			memcpy(frame->data[0]+(i*yWidth), y+(i*yWidth), yWidth);
			if (i < yHeight / 2) {
					memcpy(frame->data[1]+(i*uvWidth), u+(i*uvWidth), uvWidth);
					memcpy(frame->data[2]+(i*uvWidth), v+(i*uvWidth), uvWidth);
			}
	}
	// Encode the image
	frame->pts = 0;
	*bitstreamSize = sendFrameToNVENC(bitrate, frame, bitstream);
}

/**
 * Encode a frame twice, once at a high bitrate and once at a low one.
 */
void encodeFrame(unsigned char* y, unsigned char* u, unsigned char* v, int width,
				 int height, int bitstreamSizes[2]) {
	if (codecContextArr[0] == NULL) {
		initializeHardware();
	}
	encodeFrameWithContext(bitstreams[HIGH_BITRATE], y, u, v, width, height, HIGH_BITRATE,
						   &bitstreamSizes[HIGH_BITRATE]);
	encodeFrameWithContext(bitstreams[LOW_BITRATE], y, u, v, width, height, LOW_BITRATE,
						   &bitstreamSizes[LOW_BITRATE]);
}

/**
 * Process the command-line arguments to configure the program
 */
void processInput(Config* config, int argc, char* argv[]) {
	// Default config options
	config->highBitrate = 1600000;
	config->lowBitrate = 800000;
	config->numTileCols = 3;
	config->numTileRows = 1;
	config->inputFilename = NULL;
	config->outputFilename = NULL;
	config->width = -1;
	config->height = -1;
	config->fps = -1;
	config->tileBitratesFilename = NULL;
	// Read input
	static struct option long_options[] = {
		{"input", required_argument, 0, 'i'},
		{"output", required_argument, 0, 'o'},
		{"width", required_argument, 0, 'x'},
		{"height", required_argument, 0, 'y'},
		{"fps", required_argument, 0, 'f'},
		{"high-bitrate", required_argument, 0, 'h'},
		{"low-bitrate", required_argument, 0, 'l'},
		{"num-tile-rows", required_argument, 0, 'r'},
		{"num-tile-cols", required_argument, 0, 'c'},
		{"tile-bitrates-file", required_argument, 0, 't'},
		{0, 0, 0, 0}
	};
	int opt;
	int option_index = 0;
	while ((opt = getopt_long(argc, argv, "i:o:x:y:f:r:c:t:h:l:", long_options, NULL)) != -1) {
		switch (opt) {
			case 'i':
				config->inputFilename = optarg;
				break;
			case 'o':
				config->outputFilename = optarg;
				break;
			case 'x':
				config->width = atoi(optarg);
				break;
			case 'y':
				config->height = atoi(optarg);
				break;
			case 'f':
				config->fps = atof(optarg);
				break;
			case 'h':
				config->highBitrate = atoi(optarg);
				break;
			case 'l':
				config->lowBitrate = atoi(optarg);
				break;
			case 'r':
				config->numTileRows = atoi(optarg);
				break;
			case 'c':
				config->numTileCols = atoi(optarg);
				break;
			case 't':
				config->tileBitratesFilename = optarg;
				break;
		}
	}
	// Ensure there are no missing parameters
	if (config->inputFilename == NULL ||
		config->outputFilename == NULL ||
		config->width == -1 ||
		config->height == -1 ||
		config->fps == -1 ||
		config->tileBitratesFilename == NULL) {
		printf("Error: invalid command line parameters. Aborting.\n");
		exit(1);
	}
}

int main(int argc, char* argv[]) {
	// Process our inputs, set up our data structures
	Config* config = (Config*)malloc(sizeof(Config));
	processInput(config, argc, argv);
	FILE* inFile = fopen(config->inputFilename, "rb");
	if (inFile == NULL) {
		printf("Error: could not open input file\n");
		return 1;
	}
	FILE* outFile = fopen(config->outputFilename, "wb");
	if (outFile == NULL) {
		printf("Error: could not open output file\n");
		return 1;
	}
	bitrateValues[HIGH_BITRATE] = config->highBitrate;
	bitrateValues[LOW_BITRATE] = config->lowBitrate;
	int ySize = config->width * config->height;
	int uvSize = ySize / 4;
	int bitstreamSizes[2];
	int tiledBitstreamSize;
	int tileBitrates[] = {LOW_BITRATE, HIGH_BITRATE, LOW_BITRATE};
	unsigned char* y = malloc(sizeof(unsigned char)*ySize);
	unsigned char* u = malloc(sizeof(unsigned char)*uvSize);
	unsigned char* v = malloc(sizeof(unsigned char)*uvSize);
	bitstreams[HIGH_BITRATE] = (unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE);
	bitstreams[LOW_BITRATE] = (unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE);
	tiledBitstream = (unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE);

	// The main loop. Get a frame, rearrange it, send it to NVENC, stitch it, then write it out.
	while (getNextFrame(inFile, y, u, v, ySize)) {
		bitstreamSizes[HIGH_BITRATE] = 0;
		bitstreamSizes[LOW_BITRATE] = 0;
		rearrangeFrame(&y, &u, &v, config->width, config->height);
		encodeFrame(y, u, v, config->width/NUM_SPLITS, config->height*NUM_SPLITS, bitstreamSizes);
		tiledBitstreamSize = doStitching(tiledBitstream, bitstreams[HIGH_BITRATE],
										 bitstreams[LOW_BITRATE], bitstreamSizes[HIGH_BITRATE],
										 bitstreamSizes[LOW_BITRATE], tileBitrates);
		fwrite(tiledBitstream, sizeof(unsigned char), tiledBitstreamSize, outFile);
	}

	// Wrap up
	sendFrameToNVENC(HIGH_BITRATE, NULL, bitstreams[HIGH_BITRATE]);
    sendFrameToNVENC(LOW_BITRATE, NULL, bitstreams[LOW_BITRATE]);
	avcodec_free_context(&codecContextArr[HIGH_BITRATE]);
	avcodec_free_context(&codecContextArr[LOW_BITRATE]);
	av_frame_free(&frames[HIGH_BITRATE]);
	av_frame_free(&frames[LOW_BITRATE]);
	av_packet_free(&pkt);
	av_buffer_unref(&hwDeviceCtx);
	free(y);
	free(u);
	free(v);
	free(bitstreams[HIGH_BITRATE]);
	free(bitstreams[LOW_BITRATE]);
	free(tiledBitstream);
	fclose(inFile);
	fclose(outFile);
}

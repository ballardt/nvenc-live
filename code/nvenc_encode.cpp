#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <getopt.h>
#include <vector>

#define __STDC_CONSTANT_MACROS

extern "C"
{
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
};

#include "link_stitcher.h"

#define NUM_SPLITS (config->numTileCols)
#define BITSTREAM_SIZE 200000 // Increase if necessary; the program will let you know
#define MAX_Y_HEIGHT 8192 // Hardware limitation

using namespace std;

static AVBufferRef* hwDeviceCtx = NULL;
static enum AVPixelFormat hwPixFmt;
static int hardwareInitialized = 0;
const char* codecName;
const AVCodec* codec;
vector<vector<AVCodecContext*> > codecContextArr(2); // 1st dimension is bitrate, 2nd is context group
AVFrame* frame = NULL;
AVPacket* pkt;
enum AVHWDeviceType hwDeviceType;

vector<vector<unsigned char*> > bitstreams(2); // 1st dimension is bitrate, 2nd is context group
unsigned char* tiledBitstream;

enum Bitrate
{
	HIGH_BITRATE = 0,
	LOW_BITRATE
};
int bitrateValues[4];

struct Config
{
	char* inputFilename;
	char* outputFilename;
	int width;
	int height;
	double fps; // TODO
	int highBitrate;
	int lowBitrate;
	int numTileCols; // TODO
	int numTileRows; // TODO
	int* tileBitrates; // TODO
	int numTileBitrates;
	std::vector<ContextGroup> contextGroups;
};

int numTiles; // Should we pass instead? Makes sense to be global, but kind of sloppy

Config* config = 0;

struct Planeset
{
public:
    Planeset( int w, int h )
    {
        y = new unsigned char[w*h];
        u = new unsigned char[w*h/4];
        v = new unsigned char[w*h/4];
    }

    ~Planeset()
    {
        delete [] y;
        delete [] u;
        delete [] v;
    }

	unsigned char* y;
	unsigned char* u;
	unsigned char* v;
};

/**
 * Get the next frame, consisting of a Y, U, and V component.
 * Returns 1 if a frame was available, or 0 if there are no frames left in the file
 */
int getNextFrame(FILE* file, Planeset* ptr, int ySize)
{
	int uvSize = ySize / 4;
	int yRes, uRes, vRes;
	if (fread(ptr->y, sizeof(unsigned char), ySize, file) != ySize ||
		fread(ptr->u, sizeof(unsigned char), uvSize, file) != uvSize ||
		fread(ptr->v, sizeof(unsigned char), uvSize, file) != uvSize) {
		perror("Problem in getNextFrame");
		return 0;
	}
	return 1;
}

/**
 * Cut a frame component (Y, U, or V) into columns and stack them on top of each other.
 */
void rearrangeFrameComponent(unsigned char* inComponent, unsigned char* outComponent,
                             int origWidth, int origHeight,
							 int numSplits)
{
	int newWidth = origWidth / numSplits;
	for (int i=0; i<numSplits; i++) {
		for (int j=0; j<origHeight; j++) {
			int oldIdx = (i*newWidth) + (j*origWidth);
			int newIdx = (j*newWidth) + (i*origHeight*newWidth);
			memcpy( outComponent+newIdx, inComponent+oldIdx, newWidth ); 
		}
	}
}

/**
 * Cut a frame into columns and stack them on top of each other.
 */
void rearrangeFrame( Planeset* input, Planeset* output, int yWidth,
					int yHeight)
{
	int uvWidth = yWidth / 2;
	int uvHeight = yHeight / 2;
	rearrangeFrameComponent( input->y, output->y, yWidth, yHeight, NUM_SPLITS);
	rearrangeFrameComponent( input->u, output->u, uvWidth, uvHeight, NUM_SPLITS);
	rearrangeFrameComponent( input->v, output->v, uvWidth, uvHeight, NUM_SPLITS);
}

/**
 * Initialize the hardware context. This should be the very first thing to happen.
 */
void initializeHardware()
{
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
void initializeContext(Bitrate bitrate, int width, int height)
{
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
	int initFrame = 0;
	if (frame == NULL) {
		initFrame = 1;
		frame = av_frame_alloc();
		if (!frame) {
			printf("Error allocating frame\n");
			exit(1);
		}
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
	// Number of tiles == number of slices
	c->num_slices = numTiles;

	// Open the codec
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		printf("Error opening codec\n");
		exit(1);
	}
	// Open the frame
	if (initFrame) {
		frame->format = c->pix_fmt;
		frame->width = c->width;
		frame->height = c->height;
		ret = av_frame_get_buffer(frame, 32);
		if (ret < 0) {
			printf("Error allocating frame data\n");
			exit(1);
		}
	}
	codecContextArr[bitrate].push_back(c);
}

FILE* dbg_file = 0;

int sendFrameToNVENC(Bitrate bitrate, int contextGroupIdx, unsigned char* bitstream)
{
	int bsPos = 0;
	pkt->data = NULL;
	pkt->size = 0;
	// Send the frame
	int ret;
	if ((ret = avcodec_send_frame(codecContextArr[bitrate][contextGroupIdx], frame)) < 0) {
		printf("Error sending frame for encoding\n");
		exit(1);
	}
	// Try to receive packets until there are none
	while (ret >= 0) {
		ret = avcodec_receive_packet(codecContextArr[bitrate][contextGroupIdx], pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			// Note: we always return here
			memcpy(bitstream+bsPos, pkt->data, pkt->size);
			int frameSize = bsPos + pkt->size;
			if (frameSize > BITSTREAM_SIZE) {
				printf("ERROR: frameSize > BITSTREAM_SIZE (%d > %d)\n", frameSize, BITSTREAM_SIZE);
			}
			//bsPos += pktSize;
#if 1
			dbg_file = fopen( "writeme.hevc", "ab" );
			fwrite(pkt->data, 1, pkt->size, dbg_file);
			fclose(dbg_file);
#endif
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
#if 1
		dbg_file = fopen( "writeme.hevc", "ab" );
		fwrite(pkt->data, 1, pkt->size, dbg_file);
		fclose(dbg_file);
#endif
		av_packet_unref(pkt);
	}
}

void putImageInFrame(unsigned char* y, unsigned char* u, unsigned char* v,
					 int yWidth, int yHeight) {
	int ret;
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
	frame->pts = 0; // TODO do we need this?
}

/**
 * Encode a frame twice, once at a high bitrate and once at a low one.
 */
void encodeFrame(unsigned char* y, unsigned char* u, unsigned char* v, int width,
				 int height, vector<vector<long> > &bitstreamSizes)
{
    if( codecContextArr[0].empty() )
	// if(codecContextArr[0][0] == NULL)
    {
		initializeHardware();
		// For each context group
        std::cerr << __LINE__ << " context group size: " << config->contextGroups.size() << std::endl;
		for (int i=0; i<config->contextGroups.size(); i++) {
			initializeContext(HIGH_BITRATE, width, (config->contextGroups[i]).height);
			initializeContext(LOW_BITRATE, width, (config->contextGroups[i]).height);
		}
	}
	// For each encode group, put that image in the frame then encode it
	int currTile = 0;
	int tileHeight = height / (config->numTileRows * config->numTileCols);
	for (int i=0; i<(config->contextGroups).size(); i++) {
		// First, get the image for this encode group
		// TODO replace with Planeset. Probably put in a different function entirely when have time.
		int imageSize = config->contextGroups[i].width * config->contextGroups[i].height;
		int yOffset = 0;
		int uvOffset = 0;
		unsigned char* cgImageY = new unsigned char[imageSize];
		unsigned char* cgImageU = new unsigned char[imageSize/4];
		unsigned char* cgImageV = new unsigned char[imageSize/4];
		// Get the rest of the tiles
		int yCpySize = width * tileHeight * config->numTileRows * config->contextGroups[i].numTileCols;
		int uvCpySize = yCpySize / 4;
		memcpy(cgImageY+yOffset, y+(currTile*width*tileHeight), yCpySize);
		memcpy(cgImageU+uvOffset, u+(currTile*width*tileHeight/4), uvCpySize);
		memcpy(cgImageV+uvOffset, v+(currTile*width*tileHeight/4), uvCpySize);
		currTile += config->numTileRows * config->contextGroups[i].numTileCols;
		// Now put it in the frame and encode it
		putImageInFrame(cgImageY, cgImageU, cgImageV, width, config->contextGroups[i].height);
		bitstreamSizes[HIGH_BITRATE].push_back(sendFrameToNVENC(HIGH_BITRATE,
                                                                i,
                                                                bitstreams[HIGH_BITRATE][i]));
		bitstreamSizes[LOW_BITRATE].push_back(sendFrameToNVENC(LOW_BITRATE,
                                                               i,
                                                               bitstreams[LOW_BITRATE][i]));
	}
}

/**
 * Process tile bitrates
 * Implementation is sloppy, should be changed to be a file in the future
 */
int* processTileBitrates(char* tileBitratesStr, Config* config)
{
	int tbsLen = strlen(tileBitratesStr);
	int* tileBitrates = (int*)malloc(sizeof(int) * tbsLen);
	for (int i=0; i<strlen(tileBitratesStr); i++) {
		// The subtraction converts the '1' or '0' to an int
		tileBitrates[i] = tileBitratesStr[i] - '0';
	}
	config->numTileBitrates = tbsLen;
	return tileBitrates;
}

/**
 * Process the command-line arguments to configure the program
 */
void processInput(Config* config, int argc, char* argv[])
{
	// Default config options
	config->highBitrate = 1600000;
	config->lowBitrate  =  800000;
	config->numTileCols = 3;
	config->numTileRows = 1;
	config->inputFilename = NULL;
	config->outputFilename = NULL;
	config->width = -1;
	config->height = -1;
	config->fps = -1;
	config->tileBitrates = NULL;
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
		{"tile-bitrates", required_argument, 0, 't'},
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
				config->tileBitrates = processTileBitrates(optarg, config);
				break;
		}
	}
	// Ensure there are no missing parameters
	if (config->inputFilename == NULL ||
		config->outputFilename == NULL ||
		config->width == -1 ||
		config->height == -1 ||
		config->fps == -1 ||
		config->tileBitrates == NULL) {
		printf("Error: invalid command line parameters. Aborting.\n");
		exit(1);
	}
	int numTiles = config->numTileRows * config->numTileCols;
	if (config->numTileBitrates != numTiles) {
		printf("Error: incorrect number of tile bitrates specified. Aborting.\n");
		exit(1);
	}
}

int main(int argc, char* argv[])
{
	// Process our inputs, set up our data structures
	config = (Config*)malloc(sizeof(Config));
	processInput(config, argc, argv);
	int origHeight   = config->height;
	int paddedHeight = config->height;
	// TODO remove? since we crop first, then separate the context groups
#if 0
	while( config->numTileCols * paddedHeight > 8192 )
	{
		paddedHeight -= 1;
	}
#endif
	printf("Original height: %d Padded height: %d\n", origHeight, paddedHeight);
	while( paddedHeight % ( config->numTileRows * 32 ) != 0 )
	{
		paddedHeight -= 1;
	}
	printf("Original height: %d Padded height: %d\n", origHeight, paddedHeight);

	// Figure out how many contexts we have for each quality
	int stackHeight = paddedHeight * config->numTileCols;
	int numContextGroups = 0;
	int remainingTileCols = config->numTileCols;
	while (stackHeight > 0) {
		numContextGroups++;
		int numTileColsInContextGroup = 0;
		while ((paddedHeight * (numTileColsInContextGroup + 1) <= MAX_Y_HEIGHT)
				&& (remainingTileCols > 0)) {
			numTileColsInContextGroup++;
			remainingTileCols--;
		}
		int contextGroupHeight = paddedHeight * numTileColsInContextGroup;
		int contextGroupWidth = config->width / config->numTileCols;
		(config->contextGroups).push_back({numTileColsInContextGroup, contextGroupHeight, contextGroupWidth});
		stackHeight -= paddedHeight * numTileColsInContextGroup;
	}

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
	bitrateValues[HIGH_BITRATE]        = config->highBitrate;
	bitrateValues[LOW_BITRATE]         = config->lowBitrate;
	numTiles = config->numTileRows * config->numTileCols;
	int ySize = config->width * config->height;
	int uvSize = ySize / 4;
	vector<vector<long> > bitstreamSizes(2); // 1st dimension is quality, 2nd is contextGroup
	int tiledBitstreamSize;

	Planeset inputFrame( config->width, config->height );
	Planeset outputFrame( config->width/NUM_SPLITS, paddedHeight*NUM_SPLITS );

	for (int i=0; i<numContextGroups; i++) {
		bitstreams[HIGH_BITRATE].push_back((unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE));
		bitstreams[LOW_BITRATE].push_back((unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE));
	}
	tiledBitstream = (unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE);

	// The main loop. Get a frame, rearrange it, send it to NVENC, stitch it, then write it out.
	while (getNextFrame(inFile, &inputFrame, ySize))
    {
		// config->height = paddedHeight;
		bitstreamSizes[HIGH_BITRATE].clear();
		bitstreamSizes[LOW_BITRATE].clear();

		rearrangeFrame( &inputFrame, &outputFrame, config->width, paddedHeight );
		encodeFrame(outputFrame.y, outputFrame.u, outputFrame.v,
		            config->width/NUM_SPLITS, paddedHeight*NUM_SPLITS, bitstreamSizes);
		tiledBitstreamSize = doStitching(tiledBitstream,
                                         2,
                                         bitstreams,
                                         bitstreamSizes,
                                         config->tileBitrates,
										 config->width,
                                         paddedHeight,
                                         config->numTileRows,
										 config->numTileCols,
										 config->contextGroups);
		fwrite(tiledBitstream, sizeof(unsigned char), tiledBitstreamSize, outFile);
		// config->height = origHeight;
	}

	// Wrap up
	// TODO: put a lot of these in a big for loop iterating over the context groups
	for (int i=0; i<numContextGroups; i++) {
		sendFrameToNVENC(HIGH_BITRATE, i, bitstreams[HIGH_BITRATE][i]);
		sendFrameToNVENC(LOW_BITRATE, i, bitstreams[LOW_BITRATE][i]);
		avcodec_free_context(&codecContextArr[HIGH_BITRATE][i]);
		avcodec_free_context(&codecContextArr[LOW_BITRATE][i]);
		free(bitstreams[HIGH_BITRATE][i]);
		free(bitstreams[LOW_BITRATE][i]);
	}
	av_frame_free(&frame);
	av_packet_free(&pkt);
	av_buffer_unref(&hwDeviceCtx);
	free(tiledBitstream);
	free(config);
	fclose(inFile);
	fclose(outFile);
}


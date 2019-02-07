#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
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
#include "nvenc_config.h"
#include "nvenc_planeset.h"
#include "nvenc_hw.h"

#define NUM_SPLITS (config.numTileCols)
#define BITSTREAM_SIZE 200000 // Increase if necessary; the program will let you know
#define MAX_Y_HEIGHT 8192 // Hardware limitation

using namespace std;

static int hardwareInitialized = 0;
vector<vector<AVCodecContext*> > codecContextArr(2); // 1st dimension is bitrate, 2nd is context group

vector<vector<unsigned char*> > bitstreams(2); // 1st dimension is bitrate, 2nd is context group
unsigned char* tiledBitstream;

enum Bitrate
{
	HIGH_BITRATE = 0,
	LOW_BITRATE
};
int bitrateValues[4];

int numTiles; // Should we pass instead? Makes sense to be global, but kind of sloppy

Config config;

Hardware hw;

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

FILE* dbg_file = 0;

int sendFrameToNVENC(Bitrate bitrate, int contextGroupIdx, unsigned char* bitstream)
{
	int bsPos = 0;
	hw.pkt->data = NULL;
	hw.pkt->size = 0;
	// Send the frame
	int ret;
	if ((ret = avcodec_send_frame(codecContextArr[bitrate][contextGroupIdx], hw.frame)) < 0) {
		printf("Error sending frame for encoding\n");
		exit(1);
	}
	// Try to receive packets until there are none
	while (ret >= 0) {
		ret = avcodec_receive_packet(codecContextArr[bitrate][contextGroupIdx], hw.pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			// Note: we always return here
			memcpy(bitstream+bsPos, hw.pkt->data, hw.pkt->size);
			int frameSize = bsPos + hw.pkt->size;
			if (frameSize > BITSTREAM_SIZE) {
				printf("ERROR: frameSize > BITSTREAM_SIZE (%d > %d)\n", frameSize, BITSTREAM_SIZE);
			}
			//bsPos += pktSize;
#if 1
			dbg_file = fopen( "writeme.hevc", "ab" );
			fwrite(hw.pkt->data, 1, hw.pkt->size, dbg_file);
			fclose(dbg_file);
#endif
			av_packet_unref(hw.pkt);
			return frameSize;
		}
		else if (ret < 0) {
			printf("Error during encoding\n");
			exit(1);
		}
		// Get the data from the GPU
		// Note: this never gets executed...
		memcpy(bitstream+bsPos, hw.pkt->data, hw.pkt->size);
		bsPos += hw.pkt->size;
#if 1
		dbg_file = fopen( "writeme.hevc", "ab" );
		fwrite(hw.pkt->data, 1, hw.pkt->size, dbg_file);
		fclose(dbg_file);
#endif
		av_packet_unref(hw.pkt);
	}
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
		hw.initialize();
		// For each context group
        std::cerr << "line " << __LINE__ << " (nvenc_encode): context group size: " << config.contextGroups.size() << std::endl;
		for (int i=0; i<config.contextGroups.size(); i++) {
            codecContextArr[HIGH_BITRATE].push_back(
                hw.initializeContext(
                    bitrateValues[HIGH_BITRATE],
                    width,
                    config.contextGroups[i]->height,
                    numTiles ) );
            codecContextArr[LOW_BITRATE].push_back(
			    hw.initializeContext(
                    bitrateValues[LOW_BITRATE],
                    width,
                    config.contextGroups[i]->height,
                    numTiles ) );
		}
	}
	// For each encode group, put that image in the frame then encode it
	int currTile = 0;
	int tileHeight = height / (config.numTileRows * config.numTileCols);
	for (int i=0; i<(config.contextGroups).size(); i++) {
		// First, get the image for this encode group
		// TODO replace with Planeset. Probably put in a different function entirely when have time.
		int imageSize = config.contextGroups[i]->width * config.contextGroups[i]->height;
		int yOffset = 0;
		int uvOffset = 0;
		unsigned char* cgImageY = new unsigned char[imageSize];
		unsigned char* cgImageU = new unsigned char[imageSize/4];
		unsigned char* cgImageV = new unsigned char[imageSize/4];
		// Get the first tile if this is a subsequent group
		if (i > 0) {
			memcpy(cgImageY, y, width*tileHeight);
			memcpy(cgImageU, u, (width*tileHeight)/4);
			memcpy(cgImageV, v, (width*tileHeight)/4);
			yOffset = width*tileHeight;
			uvOffset = (width*tileHeight)/4;
		}
		// Get the rest of the tiles
		int yCpySize = width * tileHeight * config.numTileRows * config.contextGroups[i]->numTileCols;
		int uvCpySize = yCpySize / 4;
		memcpy(cgImageY+yOffset, y+(currTile*width*tileHeight), yCpySize);
		memcpy(cgImageU+uvOffset, u+(currTile*width*tileHeight/4), uvCpySize);
		memcpy(cgImageV+uvOffset, v+(currTile*width*tileHeight/4), uvCpySize);
		currTile += config.numTileRows * config.contextGroups[i]->numTileCols;
		// Now put it in the frame and encode it
		hw.putImageInFrame(cgImageY, cgImageU, cgImageV, width, config.contextGroups[i]->height);
		bitstreamSizes[HIGH_BITRATE].push_back(sendFrameToNVENC(HIGH_BITRATE,
                                                                i,
                                                                bitstreams[HIGH_BITRATE][i]));
		bitstreamSizes[LOW_BITRATE].push_back(sendFrameToNVENC(LOW_BITRATE,
                                                               i,
                                                               bitstreams[LOW_BITRATE][i]));
		delete [] cgImageY;
		delete [] cgImageU;
		delete [] cgImageV;
	}
}

int main(int argc, char* argv[])
{
	// Process our inputs, set up our data structures
	config.processInput( argc, argv );
	int origHeight   = config.height;
	int paddedHeight = config.height;
	// TODO remove? since we crop first, then separate the context groups
#if 0
	while( config.numTileCols * paddedHeight > 8192 )
	{
		paddedHeight -= 1;
	}
#endif
	printf("Original height: %d Padded height: %d\n", origHeight, paddedHeight);
	while( paddedHeight % ( config.numTileRows * 32 ) != 0 )
	{
		paddedHeight -= 1;
	}
	printf("Original height: %d Padded height: %d\n", origHeight, paddedHeight);

	// Figure out how many contexts we have for each quality
	int stackHeight = paddedHeight * config.numTileCols;
	int afterFirst = 0;
	int numContextGroups = 0;
	int remainingTileCols = config.numTileCols;
	while (stackHeight > 0) {
		numContextGroups++;
		int numTileColsInContextGroup = 0;
		// If it's after the first column group, we also add the height of the first tile in the image to skip the header stuff
		if (afterFirst == 1) {
			while (((paddedHeight * (numTileColsInContextGroup + 1))
					+ (paddedHeight / config.numTileRows) <= MAX_Y_HEIGHT)
				   && (remainingTileCols > 0)) {
				numTileColsInContextGroup++;
				remainingTileCols--;
			}
		}
		else {
			while ((paddedHeight * (numTileColsInContextGroup + 1) <= MAX_Y_HEIGHT)
				   && (remainingTileCols > 0)) {
				numTileColsInContextGroup++;
				remainingTileCols--;
			}
		}
		int contextGroupHeight = paddedHeight * numTileColsInContextGroup + (afterFirst == 0 ? 0 : (paddedHeight / config.numTileRows));
		int contextGroupWidth = config.width / config.numTileCols;
        std::shared_ptr<ContextGroup> ptr = std::make_shared<ContextGroup>(
                                                    numTileColsInContextGroup,
                                                    contextGroupHeight,
                                                    contextGroupWidth );
		config.contextGroups.push_back( ptr );

		stackHeight -= paddedHeight * numTileColsInContextGroup;
		if (afterFirst == 0 && stackHeight > 0) {
			afterFirst = 1;
		}
	}

	FILE* inFile = fopen(config.inputFilename, "rb");
	if (inFile == NULL) {
		printf("Error: could not open input file\n");
		return 1;
	}
	FILE* outFile = fopen(config.outputFilename, "wb");
	if (outFile == NULL) {
		printf("Error: could not open output file\n");
		return 1;
	}
	bitrateValues[HIGH_BITRATE]        = config.highBitrate;
	bitrateValues[LOW_BITRATE]         = config.lowBitrate;
	numTiles = config.numTileRows * config.numTileCols;
	int ySize = config.width * config.height;
	int uvSize = ySize / 4;
	vector<vector<long> > bitstreamSizes(2); // 1st dimension is quality, 2nd is contextGroup
	int tiledBitstreamSize;

	Planeset inputFrame( config.width, config.height );
	Planeset outputFrame( config.width/NUM_SPLITS, paddedHeight*NUM_SPLITS );

	for (int i=0; i<numContextGroups; i++) {
		bitstreams[HIGH_BITRATE].push_back((unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE));
		bitstreams[LOW_BITRATE].push_back((unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE));
	}
	tiledBitstream = (unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE);

	// The main loop. Get a frame, rearrange it, send it to NVENC, stitch it, then write it out.
	while (getNextFrame(inFile, &inputFrame, ySize))
    {
		// config.height = paddedHeight;
		bitstreamSizes[HIGH_BITRATE].clear();
		bitstreamSizes[LOW_BITRATE].clear();

		rearrangeFrame( &inputFrame, &outputFrame, config.width, paddedHeight );
		encodeFrame(outputFrame.y, outputFrame.u, outputFrame.v,
		            config.width/NUM_SPLITS, paddedHeight*NUM_SPLITS, bitstreamSizes);
		tiledBitstreamSize = doStitching(tiledBitstream,
                                         2,
                                         bitstreams,
                                         bitstreamSizes,
                                         config.tileBitrates,
										 config.width,
                                         paddedHeight,
                                         config.numTileRows,
										 config.numTileCols,
										 config.contextGroups);
		fwrite(tiledBitstream, sizeof(unsigned char), tiledBitstreamSize, outFile);
		// config.height = origHeight;
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
	free(tiledBitstream);
	fclose(inFile);
	fclose(outFile);
}


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
#include "nvenc_bitrates.h"
#include "filereader.h"

#define NUM_SPLITS (config.numTileCols)
#define BITSTREAM_SIZE 200000 // Increase if necessary; the program will let you know
#define MAX_Y_HEIGHT 4096 // Hardware limitation

using namespace std;

int CG_DEPTH = 1;
int CG_LOCK  = 0;

static int hardwareInitialized = 0;

unsigned char* tiledBitstream;

int bitrateValues[4];

Config config;

Hardware hw;

FILE* dbg_file = 0;

int sendFrameToNVENC(Bitrate bitrate, int contextGroupIdx)
{

    unsigned char* bitstream = config.contextGroups[contextGroupIdx]->getBitstream(bitrate);

	int bsPos = 0;
	hw.pkt->data = NULL;
	hw.pkt->size = 0;
	// Send the frame
	int ret;
	if ((ret = avcodec_send_frame(
                    config.contextGroups[contextGroupIdx]->getContext(bitrate),
                    hw.frame)) < 0)
    {
		printf("Error sending frame for encoding\n");
		exit(1);
	}

	// Try to receive packets until there are none
	printf("About to receive packets!\n");
	while (ret >= 0) {
		ret = avcodec_receive_packet(
                    config.contextGroups[contextGroupIdx]->getContext(bitrate),
                    hw.pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
			// Note: we always return here
			memcpy(bitstream+bsPos, hw.pkt->data, hw.pkt->size);
			int frameSize = bsPos + hw.pkt->size;
			if (frameSize > BITSTREAM_SIZE) {
				printf("ERROR: frameSize > BITSTREAM_SIZE (%d > %d)\n", frameSize, BITSTREAM_SIZE);
			}
			//bsPos += pktSize;
#if 1
			printf("Writing to dbg_file...\n");
			dbg_file = fopen( "writeme.h264", "ab" );
			fwrite(hw.pkt->data, 1, hw.pkt->size, dbg_file);
			fclose(dbg_file);
#endif
			av_packet_unref(hw.pkt);
			return frameSize;
		}
		else if (ret < 0)
        {
			printf("Error during encoding\n");
			exit(1);
		}
        else
        {
		    // Get the data from the GPU
		    // Note: this never gets executed...
		    memcpy(bitstream+bsPos, hw.pkt->data, hw.pkt->size);
		    bsPos += hw.pkt->size;
#if 1
		    dbg_file = fopen( "writeme.h264", "ab" );
		    fwrite(hw.pkt->data, 1, hw.pkt->size, dbg_file);
		    fclose(dbg_file);
#endif
		    av_packet_unref(hw.pkt);
        }
	}
}

/**
 * Encode a frame twice, once at a high bitrate and once at a low one.
 */
void encodeFrame(unsigned char* y, unsigned char* u, unsigned char* v, int width, int height )
{
    static bool first_time = true;
    if( first_time )
    {
        first_time = false;

		hw.initialize();
		// For each context group
        std::cerr << "line " << __LINE__ << " (nvenc_encode): context group size: " << config.contextGroups.size() << std::endl;
		for (int i=0; i<config.contextGroups.size(); i++) {
            config.contextGroups[i]->setContext( HIGH_BITRATE,
                hw.initializeContext(
                    bitrateValues[HIGH_BITRATE],
                    width,
                    config.contextGroups[i]->getHeight(),
                    config.contextGroups[i]->getNumTileCols() * config.numTileRows ) );
            config.contextGroups[i]->setContext( LOW_BITRATE,
			    hw.initializeContext(
                    bitrateValues[LOW_BITRATE],
                    width,
                    config.contextGroups[i]->getHeight(),
                    config.contextGroups[i]->getNumTileCols() * config.numTileRows ) );
		}
	}
	// For each encode group, put that image in the frame then encode it
	int currTile = 0;
	int tileHeight = height / (config.numTileRows * config.numTileCols);

	for (int i=0; i<config.contextGroups.size(); i++) {
		// First, get the image for this encode group
		// TODO replace with Planeset. Probably put in a different function entirely when have time.
		int yOffset = 0;
		int uvOffset = 0;

        Planeset& cgImage = config.contextGroups[i]->getPlaneset( );

#if 1
        if( width > config.contextGroups[i]->getWidth() )
        {
            std::cerr << "line " << __LINE__ << " (nvenc_encode.cpp): " << "tile width " << width << " is larger than expected context group width " << config.contextGroups[i]->getWidth() << std::endl;
            exit( -1 );
        }
        if( tileHeight > config.contextGroups[i]->getHeight() )
        {
            std::cerr << "line " << __LINE__ << " (nvenc_encode.cpp): " << "tile height " << tileHeight << " is larger than expected context group height " << config.contextGroups[i]->getHeight() << std::endl;
            exit( -1 );
        }
#endif
		// Get the first tile if this is a subsequent group
		//if (i > 0) {
		//	memcpy(cgImage.y, y, width*tileHeight);
		//	memcpy(cgImage.u, u, (width*tileHeight)/4);
		//	memcpy(cgImage.v, v, (width*tileHeight)/4);
		//	yOffset = width*tileHeight;
		//	uvOffset = (width*tileHeight)/4;
		//}
		// Get the rest of the tiles
		int yCpySize = width * tileHeight * config.numTileRows * config.contextGroups[i]->numTileCols;
		int uvCpySize = yCpySize / 4;
		memcpy(cgImage.y+yOffset, y+(currTile*width*tileHeight), yCpySize);
		memcpy(cgImage.u+uvOffset, u+(currTile*width*tileHeight/4), uvCpySize);
		memcpy(cgImage.v+uvOffset, v+(currTile*width*tileHeight/4), uvCpySize);
		currTile += config.numTileRows * config.contextGroups[i]->numTileCols;

		// Now put it in the frame and encode it
		hw.putImageInFrame(cgImage.y, cgImage.u, cgImage.v, width, config.contextGroups[i]->getHeight());

		config.contextGroups[i]->setBitstreamSize(
			HIGH_BITRATE,
			sendFrameToNVENC( HIGH_BITRATE, i ) );
		config.contextGroups[i]->setBitstreamSize(
			LOW_BITRATE,
			sendFrameToNVENC( LOW_BITRATE,  i ) );
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
	while( config.numTileCols * paddedHeight > MAX_Y_HEIGHT)
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
	//int afterFirst = 0;
	int numContextGroups = 0;
	int remainingTileCols = config.numTileCols;
	while (stackHeight > 0) {
		numContextGroups++;
		int numTileColsInContextGroup = 0;
		// If it's after the first column group, we also add the height of the first tile in the image to skip the header stuff
		//if (afterFirst == 1) {
		//	while (((paddedHeight * (numTileColsInContextGroup + 1))
		//			+ (paddedHeight / config.numTileRows) <= MAX_Y_HEIGHT)
		//		   && (remainingTileCols > 0)) {
		//		numTileColsInContextGroup++;
		//		remainingTileCols--;
		//	}
		//}
		//else {
		while ((paddedHeight * (numTileColsInContextGroup + 1) <= MAX_Y_HEIGHT)
				&& (remainingTileCols > 0)) {
			numTileColsInContextGroup++;
			remainingTileCols--;
		}
		//}
		//int contextGroupHeight = paddedHeight * numTileColsInContextGroup + (afterFirst == 0 ? 0 : (paddedHeight / config.numTileRows));
		int contextGroupHeight = paddedHeight * numTileColsInContextGroup;
		int contextGroupWidth = config.width / config.numTileCols;
        std::shared_ptr<ContextGroup> ptr = std::make_shared<ContextGroup>(
                                                    numTileColsInContextGroup,
                                                    contextGroupHeight,
                                                    contextGroupWidth );
		config.contextGroups.push_back( ptr );

		stackHeight -= paddedHeight * numTileColsInContextGroup;
		//if (afterFirst == 0 && stackHeight > 0) {
		//	afterFirst = 1;
		//}
	}

	bitrateValues[HIGH_BITRATE]        = config.highBitrate;
	bitrateValues[LOW_BITRATE]         = config.lowBitrate;
	int ySize = config.width * config.height;
	int uvSize = ySize / 4;
	int tiledBitstreamSize;

    FileReader* fr = new FileReader( config.inputFilename, config.width, config.height, paddedHeight, NUM_SPLITS );
    if( !fr->ok() ) {
        return 1;
    }

	FILE* outFile = fopen(config.outputFilename, "wb");
	if (outFile == NULL) {
		printf("Error: could not open output file\n");
		return 1;
	}

    // Planeset* inputFrame = 0;
	// Planeset outputFrame( config.width/NUM_SPLITS, paddedHeight*NUM_SPLITS );
	Planeset* outputFrame = 0;

	for (int i=0; i<numContextGroups; i++) {
        config.contextGroups[i]->setBufferSize( BITSTREAM_SIZE );
	}
	//tiledBitstream = (unsigned char*)malloc(sizeof(unsigned char) * BITSTREAM_SIZE);
	char buffer[50];
	//FILE* outFiles[numContextGroups * 2];
	FILE* outFiles[numContextGroups][2];
	for( int i=0; i<numContextGroups; i++ ) {
		sprintf(buffer, "cg%d_q%d.h264", i, 0);
		//outFiles[(2*i)] = 
		outFiles[i][0] = fopen(buffer, "wb");
		sprintf(buffer, "cg%d_q%d.h264", i, 1);
		//outFiles[(2*i)+1] = 
		outFiles[i][1] = fopen(buffer, "wb");
	}

	// The main loop. Get a frame, rearrange it, send it to NVENC, stitch it, then write it out.
	while( outputFrame = fr->getNextFrame(ySize) )
    {
		// config.height = paddedHeight;
        for( auto group : config.contextGroups )
            group->clearBitstreamSizes();

		encodeFrame( outputFrame->y,
                     outputFrame->u,
                     outputFrame->v,
                     config.width/NUM_SPLITS,
                     paddedHeight*NUM_SPLITS );
		/*tiledBitstreamSize = doStitching(tiledBitstream,
                                         2,
                                         config.tileBitrates,
										 config.width/NUM_SPLITS,
										 paddedHeight*NUM_SPLITS,
                                         config.numTileRows,
										 config.numTileCols,
										 config.contextGroups);
		fwrite(tiledBitstream, sizeof(unsigned char), tiledBitstreamSize, outFile);
		*/
		for (int i=0; i<numContextGroups; i++) {
			fwrite(config.contextGroups[i]->getBitstream(HIGH_BITRATE), sizeof(unsigned char), config.contextGroups[i]->getBitstreamSize(HIGH_BITRATE), outFiles[i][0]);
			fwrite(config.contextGroups[i]->getBitstream(LOW_BITRATE), sizeof(unsigned char), config.contextGroups[i]->getBitstreamSize(LOW_BITRATE), outFiles[i][1]);
		}
		// config.height = origHeight;
	}

	// Wrap up
	// TODO: put a lot of these in a big for loop iterating over the context groups
	for (int i=0; i<numContextGroups; i++) {
		sendFrameToNVENC( HIGH_BITRATE, i );
		sendFrameToNVENC( LOW_BITRATE, i );
	}

    for( auto group : config.contextGroups )
        group->release();

	free(tiledBitstream);
	fclose(outFile);
}


#pragma once

#include <vector>

using namespace std;

struct ContextGroup
{
	int numTileCols;
	int height; // INCLUDES the extra tile for groups which have it
	int width;
};


int doStitching( unsigned char* tiledBitstream,
                 int            numQualityLevels,
                 vector<vector<unsigned char*> >& bitstreams,
                 vector<vector<long> >& bitstream_Size,
                 int*           tileBitrates,
                 int            finalWidth,
                 int            finalHeight,
                 int            numTileRows,
                 int            numTileCols,
                 vector<ContextGroup>& contextGroups);

// extern unsigned char* bitstreams[4];
extern unsigned char* tiledBitstream;


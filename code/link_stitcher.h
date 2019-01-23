#pragma once

#include <vector>

#include "context_group.h"

using namespace std;

int doStitching( unsigned char* tiledBitstream,
                 int            numQualityLevels,
                 vector<vector<unsigned char*> >& bitstreams,
                 vector<vector<long> >&           bitstream_Size,
                 const vector<int>&               tileBitrates,
                 int            finalWidth,
                 int            finalHeight,
                 int            numTileRows,
                 int            numTileCols,
                 vector<ContextGroup>& contextGroups);

// extern unsigned char* bitstreams[4];
extern unsigned char* tiledBitstream;


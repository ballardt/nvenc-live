#pragma once

#include <vector>
#include <memory>

#include "context_group.h"

int doStitching( unsigned char* tiledBitstream,
                 int            numQualityLevels,
                 std::vector<std::vector<unsigned char*> >& bitstreams,
                 std::vector<std::vector<long> >& bitstream_Size,
                 int*           tileBitrates,
                 int            finalWidth,
                 int            finalHeight,
                 int            numTileRows,
                 int            numTileCols,
                 std::vector<std::shared_ptr<ContextGroup> >& contextGroups);

// extern unsigned char* bitstreams[4];
extern unsigned char* tiledBitstream;


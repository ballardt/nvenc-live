#pragma once

#include <vector>

#include "context_group.h"

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
    
    void processInput(int argc, char* argv[]);
    int* processTileBitrates( char* tileBitratesStr );
};


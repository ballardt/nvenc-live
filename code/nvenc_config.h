#pragma once

#include <vector>
#include <memory>

#include "context_group.h"

class Config
{
public:
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
	std::vector<std::shared_ptr<ContextGroup> > contextGroups;
    
    void processInput(int argc, char* argv[]);
    int* processTileBitrates( char* tileBitratesStr );

    int getNumTiles() const;
};


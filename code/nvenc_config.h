#pragma once

#include <vector>

#include "context_group.h"

class Config
{
	char* inputFilename;
	char* outputFilename;
	int width;
	int height;
	// double fps; // TODO
	int highBitrate;
	int lowBitrate;
	int numTileCols; // TODO
	int numTileRows; // TODO
	std::vector<int> tileBitrates; // TODO
	// int numTileBitrates;

public:
	std::vector<ContextGroup> contextGroups;
    
public:
    void processInput(int argc, char* argv[]);
    void processTileBitrates( char* tileBitratesStr );

    inline const char* getInputFilename() const { return inputFilename; }
    inline const char* getOutputFilename() const { return outputFilename; }
    inline int getHighBitrate() const { return highBitrate; }
    inline int getLowBitrate()  const { return lowBitrate; }
    inline int getWidth()       const { return width; }
    inline int getHeight()      const { return height; }
    inline int getNumTileRows() const { return numTileRows; }
    inline int getNumTileCols() const { return numTileCols; }
    inline const std::vector<int>& getTileBitrates() const { return tileBitrates; }
};


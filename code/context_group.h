#pragma once

struct ContextGroup
{
	int numTileCols;
	int height; // INCLUDES the extra tile for groups which have it
	int width;

    inline int getNumTileCols() const
    {
        return numTileCols;
    }
};


#pragma once

struct ContextGroup
{
	int numTileCols;
	int height; // INCLUDES the extra tile for groups which have it
	int width;

    ContextGroup( int n, int h, int w );
};

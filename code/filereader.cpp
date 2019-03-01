#include <string.h>

#include "filereader.h"

FileReader::FileReader( size_t width, size_t height, size_t padded_height, int num_splits )
    : _orig_width(width)
    , _orig_height(height)
    , _padded_height( padded_height )
    , _num_splits( num_splits )
{
    _stacked_width  = _orig_width    / _num_splits;
    _stacked_height = _padded_height * _num_splits;
    _orig_cache.push_back( new Planeset( _orig_width, _orig_height ) );
    _stacked_cache.push_back( new Planeset( _stacked_width, _stacked_height ) );
}

FileReader::~FileReader( )
{
    for( auto ptr : _orig_cache )
    {
        delete ptr;
    }
    _orig_cache.clear();
}

/**
 * Get the next frame, consisting of a Y, U, and V component.
 * Returns a frame pointer if a frame was available, or 0 if there are no frames
 * left in the file
 */
Planeset* FileReader::getNextFrame(FILE* file, int ySize)
{
	Planeset* ptr = _orig_cache[0];

	int uvSize = ySize / 4;
	// int yRes, uRes, vRes;
	if (fread(ptr->y, sizeof(unsigned char), ySize, file) != ySize ||
		fread(ptr->u, sizeof(unsigned char), uvSize, file) != uvSize ||
		fread(ptr->v, sizeof(unsigned char), uvSize, file) != uvSize) {
		perror("Problem in getNextFrame");
		return 0;
	}

    return rearrangeFrame( ptr );
}

Planeset* FileReader::rearrangeFrame( Planeset* input )
{
    Planeset* rearranged = _stacked_cache[0];

    const int yWidth  = _orig_width;
	const int uvWidth = yWidth / 2;
    const int yHeight  = _padded_height;
	const int uvHeight = yHeight / 2;
	rearrangeFrameComponent( input->y, rearranged->y, yWidth, yHeight, _num_splits);
	rearrangeFrameComponent( input->u, rearranged->u, uvWidth, uvHeight, _num_splits);
	rearrangeFrameComponent( input->v, rearranged->v, uvWidth, uvHeight, _num_splits);

    return rearranged;
}

void FileReader::rearrangeFrameComponent( unsigned char* inComponent,
                                          unsigned char* outComponent,
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


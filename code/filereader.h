#pragma once

#include <stdio.h>
#include <vector>

#include "nvenc_planeset.h"

class FileReader
{
public:
    FileReader( size_t width, size_t height, size_t padded_height, int num_splits );
    ~FileReader( );

    Planeset* getNextFrame(FILE* file, int ySize);

private:
    /**
     * Cut a frame into columns and stack them on top of each other.
     */
    Planeset* rearrangeFrame( Planeset* input );

    /**
     * Cut a frame component (Y, U, or V) into columns and stack them on top of each other.
     */
    void rearrangeFrameComponent( unsigned char* inComponent,
                                  unsigned char* outComponent,
                                  int origWidth, int origHeight,
                                  int numSplits );

private:
    size_t                 _orig_width;
    size_t                 _orig_height;
    size_t                 _padded_height;
    int                    _num_splits;
    size_t                 _stacked_width;
    size_t                 _stacked_height;
    std::vector<Planeset*> _orig_cache;
    std::vector<Planeset*> _stacked_cache;
};


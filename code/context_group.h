#pragma once

#include <map>

#include "nvenc_bitrates.h"

struct AVCodecContext;

class ContextGroup
{
public:
	int numTileCols;
	int height; // INCLUDES the extra tile for groups which have it
	int width;

    ContextGroup( int n, int h, int w );

    void            setContext( Bitrate b, AVCodecContext* ctx );
    AVCodecContext* getContext( Bitrate b );
    void            freeContexts( );
    bool            hasContexts( ) const;

private:
    std::map<Bitrate,AVCodecContext*> contexts;

    typedef std::map <Bitrate,AVCodecContext*> Map;
    typedef std::pair<Bitrate,AVCodecContext*> Pair;

};


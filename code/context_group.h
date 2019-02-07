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

    void            createBitstream( Bitrate b, size_t sz );
    unsigned char*  getBitstream( Bitrate b );
    void            freeBitstreams( );

private:
    std::map<Bitrate,AVCodecContext*> contexts;

    typedef std::map <Bitrate,AVCodecContext*> Map;
    typedef std::pair<Bitrate,AVCodecContext*> Pair;

    std::map<Bitrate,unsigned char*> bitstreams;

    typedef std::map <Bitrate,unsigned char*> BMap;
    typedef std::pair<Bitrate,unsigned char*> BPair;
};


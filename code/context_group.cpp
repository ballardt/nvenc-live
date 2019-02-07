#include <iostream>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavutil/common.h"
#include "libavcodec/avcodec.h"
// #include "libavdevice/avdevice.h"
// #include "libavfilter/avfilter.h"
// #include "libavformat/avformat.h"
// #include "libswresample/swresample.h"
// #include "libavutil/avutil.h"
// #include "libavutil/hwcontext.h"
// #include "libswscale/swscale.h"
// #include "libpostproc/postprocess.h"
};

#include "context_group.h"

ContextGroup::ContextGroup( int n, int h, int w )
    : numTileCols(n)
    , height(h)
    , width(w)
{ }

void ContextGroup::setContext( Bitrate b, AVCodecContext* ctx )
{
    Map::iterator it;
    it = contexts.find( b );
    if( it == contexts.end() )
    {
        contexts.insert( Pair( b, ctx ) );
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to insert hardware context twice for the same bitrate " << b << " - ignoring" << std::endl;
        avcodec_free_context( &ctx );
    }
}

AVCodecContext* ContextGroup::getContext( Bitrate b )
{
    Map::iterator it;
    it = contexts.find( b );
    if( it != contexts.end() )
    {
        return it->second;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get hardware context for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

bool ContextGroup::hasContexts( ) const
{
    return ( contexts.empty() == false );
}

void ContextGroup::freeContexts( )
{
    Map::iterator it  = contexts.begin();
    Map::iterator end = contexts.end();
    while( it != end )
    {
        avcodec_free_context( &it->second );
        it++;
    }
    contexts.clear();
}

void ContextGroup::createBitstream( Bitrate b, size_t sz )
{
    BMap::iterator it;
    it = bitstreams.find( b );
    if( it == bitstreams.end() )
    {
        bitstreams.insert( BPair( b, new unsigned char[sz] ) );
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to insert bitstream twice for the same bitrate " << b << " - ignoring" << std::endl;
    }
}

unsigned char* ContextGroup::getBitstream( Bitrate b )
{
    BMap::iterator it;
    it = bitstreams.find( b );
    if( it != bitstreams.end() )
    {
        return it->second;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get bitstream for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

void ContextGroup::freeBitstreams( )
{
    BMap::iterator it  = bitstreams.begin();
    BMap::iterator end = bitstreams.end();
    while( it != end )
    {
        delete [] it->second;
        it++;
    }
    bitstreams.clear();
}


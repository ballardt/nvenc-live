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
    : _valid( true )
    , numTileCols(n)
    , height(h)
    , width(w)
    , cgImage( w, h )
{
    std::cerr << "line " << __LINE__ << " (context_group.cpp): creating a context group with width " << w << " and height " << h << std::endl;
}

ContextGroup::~ContextGroup( )
{
    _valid = false;
}

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

/*************************************************************
 * bitstream sizes
 *************************************************************/
void ContextGroup::setBitstreamSize( Bitrate b, long sz )
{
    SMap::iterator it;
    it = bitstreamSizes.find( b );
    if( it == bitstreamSizes.end() )
    {
        bitstreamSizes.insert( SPair( b, sz ) );
    }
    else
    {
        it->second = sz;
    }
}

void ContextGroup::incBitstreamSize( Bitrate b, long sz )
{
    SMap::iterator it;
    it = bitstreamSizes.find( b );
    if( it == bitstreamSizes.end() )
    {
        bitstreamSizes.insert( SPair( b, sz ) );
    }
    else
    {
        it->second += sz;
    }
}

long ContextGroup::getBitstreamSize( Bitrate b ) const
{
    SMap::const_iterator it;
    it = bitstreamSizes.find( b );
    if( it != bitstreamSizes.end() )
    {
        return it->second;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get bitstreamSize for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

void ContextGroup::clearBitstreamSizes( )
{
    bitstreamSizes.clear();
}

/*************************************************************
 * bitstream pos
 *************************************************************/
void ContextGroup::setBitstreamPos( Bitrate b, long sz )
{
    SMap::iterator it;
    it = bitstreamPos.find( b );
    if( it == bitstreamPos.end() )
    {
        bitstreamPos.insert( SPair( b, sz ) );
    }
    else
    {
        it->second = sz;
    }
}

void ContextGroup::incBitstreamPos( Bitrate b, long sz )
{
    SMap::iterator it;
    it = bitstreamPos.find( b );
    if( it == bitstreamPos.end() )
    {
        bitstreamPos.insert( SPair( b, sz ) );
    }
    else
    {
        it->second += sz;
    }
}

long ContextGroup::getBitstreamPos( Bitrate b ) const
{
    SMap::const_iterator it;
    it = bitstreamPos.find( b );
    if( it != bitstreamPos.end() )
    {
        return it->second;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get bitstreamPos for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

long& ContextGroup::getBitstreamPosRef( Bitrate b )
{
    SMap::iterator it;
    it = bitstreamPos.find( b );
    if( it != bitstreamPos.end() )
    {
        return it->second;
    }
    else
    {
        bitstreamPos.insert( SPair( b, 0 ) );
        it = bitstreamPos.find( b );
        return it->second;
    }
}

void ContextGroup::clearBitstreamPos( )
{
    bitstreamPos.clear();
}

/*************************************************************
 * planeset
 *************************************************************/
Planeset& ContextGroup::getPlaneset()
{
    return cgImage;
}


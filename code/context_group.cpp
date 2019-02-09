#include <iostream>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavutil/common.h"
#include "libavcodec/avcodec.h"
};

#include "context_group.h"

Context::Context( )
    : ctx( 0 )
    , stream( 0 )
    , size( 0 )
    , pos( 0 )
{ }

Context::~Context( )
{
    avcodec_free_context( &ctx );
    delete [] stream;
}

ContextGroup::ContextGroup( int n, int h, int w )
    : _valid( true )
    , numTileCols(n)
    , height(h)
    , width(w)
    , _bitstreamSize( 0 )
    , cgImage( w, h )
{
    std::cerr << "line " << __LINE__ << " (context_group.cpp): creating a context group with width " << w << " and height " << h << std::endl;
}

ContextGroup::~ContextGroup( )
{
    release();
    _valid = false;
}

void ContextGroup::release( )
{
    for( auto c : _ctx ) delete c.second;
    _ctx.clear();
}

void ContextGroup::setBufferSize( size_t sz )
{
    _bitstreamSize = sz;
}

void ContextGroup::setContext( Bitrate b, AVCodecContext* ctx )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it == _ctx.end() )
    {
        Context* c = new Context;
        c->ctx = ctx;
        c->stream = new unsigned char[_bitstreamSize];
        _ctx.insert( Pair( b, c ) );
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
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        return it->second->ctx;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get hardware context for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

unsigned char ContextGroup::getBitstreamHere( Bitrate b )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        unsigned char* stream = it->second->stream;
        long           pos    = it->second->pos;
        return stream[pos];
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get bitstream for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

unsigned char* ContextGroup::getBitstream( Bitrate b )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        return it->second->stream;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to get bitstream for bitrate " << b << " that does not exist - returning 0" << std::endl;
        return 0;
    }
}

/*************************************************************
 * bitstream sizes
 *************************************************************/
void ContextGroup::setBitstreamSize( Bitrate b, long sz )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        it->second->size = sz;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to set size on non-existent context - ignoring" << std::endl;
    }
}

void ContextGroup::incBitstreamSize( Bitrate b, long sz )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        it->second->size += sz;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to increase size on non-existent context - ignoring" << std::endl;
    }
}

long ContextGroup::getBitstreamSize( Bitrate b ) const
{
    Map::const_iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        return it->second->size;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to read size from non-existent context - ignoring" << std::endl;
        return 0;
    }
}

void ContextGroup::clearBitstreamSizes( )
{
    for( auto c : _ctx )
    {
        c.second->size = 0;
    }
}

/*************************************************************
 * bitstream pos
 *************************************************************/
void ContextGroup::setBitstreamPos( Bitrate b, long sz )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        it->second->pos = sz;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to set pos on non-existent context - ignoring" << std::endl;
    }
}

void ContextGroup::incBitstreamPos( Bitrate b, long sz )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        it->second->pos += sz;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to increase pos on non-existent context - ignoring" << std::endl;
    }
}

long ContextGroup::getBitstreamPos( Bitrate b )
{
    Map::iterator it;
    it = _ctx.find( b );
    if( it != _ctx.end() )
    {
        return it->second->pos;
    }
    else
    {
        std::cerr << "line " << __LINE__ << " (context_group.cpp): trying to read pos from non-existent context - ignoring" << std::endl;
        return 0;
    }
}

void ContextGroup::clearBitstreamPos( )
{
    for( auto c : _ctx )
    {
        c.second->pos = 0;
    }
}

/*************************************************************
 * planeset
 *************************************************************/
Planeset& ContextGroup::getPlaneset()
{
    return cgImage;
}


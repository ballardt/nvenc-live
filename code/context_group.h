#pragma once

#include <map>
// #include <memory>

#include "nvenc_bitrates.h"
#include "nvenc_planeset.h"

struct AVCodecContext;

class ContextGroup
{
public:
	int      numTileCols;
	int      height; // INCLUDES the extra tile for groups which have it
	int      width;
    Planeset cgImage;

    ContextGroup( int n, int h, int w );
    ~ContextGroup( );

    void            setContext( Bitrate b, AVCodecContext* ctx );
    AVCodecContext* getContext( Bitrate b );
    void            freeContexts( );
    bool            hasContexts( ) const;

    void            createBitstream( Bitrate b, size_t sz );
    unsigned char*  getBitstream( Bitrate b );
    void            freeBitstreams( );

    void            setBitstreamSize( Bitrate b, long val );
    void            incBitstreamSize( Bitrate b, long val );
    long            getBitstreamSize( Bitrate b ) const;
    void            clearBitstreamSizes( );

    Planeset&       getPlaneset();

private:
    std::map<Bitrate,AVCodecContext*> contexts;

    typedef std::map <Bitrate,AVCodecContext*> Map;
    typedef std::pair<Bitrate,AVCodecContext*> Pair;

    std::map<Bitrate,unsigned char*> bitstreams;

    typedef std::map <Bitrate,unsigned char*> BMap;
    typedef std::pair<Bitrate,unsigned char*> BPair;

    std::map<Bitrate,long> bitstreamSizes;

    typedef std::map <Bitrate,long> SMap;
    typedef std::pair<Bitrate,long> SPair;
};


#pragma once

#include <map>
// #include <memory>

#include "nvenc_bitrates.h"
#include "nvenc_planeset.h"

struct AVCodecContext;

struct Context
{
    AVCodecContext* ctx;
    unsigned char*  stream;
    long            size;
    long            pos;

    Context();
    ~Context();
};

class ContextGroup
{
    bool     _valid;
public:
	int      numTileCols;
private:
	int      height; // INCLUDES the extra tile for groups which have it
	int      width;
public:
    Planeset cgImage;

    ContextGroup( int n, int h, int w );
    ~ContextGroup( );

    inline bool valid() const { return _valid; }

    void            release( );

    void            setBufferSize( size_t val );

    inline int      getHeight() const { return height; }
    inline int      getWidth()  const { return width; }

    void            setContext( Bitrate b, AVCodecContext* ctx );
    AVCodecContext* getContext( Bitrate b );

    unsigned char*  getBitstream( Bitrate b );
    unsigned char   getBitstreamHere( Bitrate b ); // return char at current bitstream_pos

    void            setBitstreamSize( Bitrate b, long val );
    void            incBitstreamSize( Bitrate b, long val );
    long            getBitstreamSize( Bitrate b ) const;
    void            clearBitstreamSizes( );

    void            setBitstreamPos( Bitrate b, long val );
    void            incBitstreamPos( Bitrate b, long val );
    long            getBitstreamPos( Bitrate b );
    void            clearBitstreamPos( );

    Planeset&       getPlaneset();

private:
    typedef std::map <Bitrate,Context*> Map;
    typedef std::pair<Bitrate,Context*> Pair;

    size_t _bitstreamSize;
    Map    _ctx;
};


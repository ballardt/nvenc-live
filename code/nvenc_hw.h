#pragma once

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include "libavutil/common.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"
};

class Hardware
{
    AVBufferRef* hwDeviceCtx;
    enum AVHWDeviceType hwDeviceType;
    const AVCodec* codec;
    const char* codecName;

public:
    AVFrame* frame = NULL;
    AVPacket* pkt;

public:
    Hardware();
    ~Hardware();

    void initialize();

    AVCodecContext* initializeContext(int bitrateValue, int width, int height, int numTiles);

    void putImageInFrame( const unsigned char* y,
                          const unsigned char* u,
                          const unsigned char* v,
                          int yWidth,
                          int yHeight );
};


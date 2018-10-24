#ifdef __cplusplus
extern "C" {
#endif

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

static AVBufferRef* hwDeviceCtx;
static enum AVPixelFormat hwPixFmt;
const char* codecName;
const AVCodec* codec;
AVCodecContext* codecContextArr[2];
AVFrame* frames[2];
AVPacket* pkt;
enum AVHWDeviceType hwDeviceType;

typedef enum bitrate {
	HIGH_BITRATE = 0,
	LOW_BITRATE
} Bitrate;

int bitrateValues[2];
FILE* outFiles[2];

#ifdef __cplusplus
}
#endif	

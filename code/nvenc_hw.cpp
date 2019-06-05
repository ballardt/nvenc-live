#include <stdio.h>
#include <stdlib.h>

#include "nvenc_hw.h"

static enum AVPixelFormat hwPixFmt;

/**
 * This does not get called directly, 
 */
static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hwPixFmt)
            return *p;
    }
	printf("Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

Hardware::Hardware()
{
    hwDeviceCtx = NULL;
    frame = 0;
    pkt = 0;
}

Hardware::~Hardware()
{
	av_frame_free(&frame);
	av_packet_free(&pkt);
	av_buffer_unref(&hwDeviceCtx);
}

/**
 * Initialize the hardware context. This should be the very first thing to happen.
 */
void Hardware::initialize()
{
	av_log_set_level(40);
	// Load the hardware
	hwDeviceType = av_hwdevice_find_type_by_name("cuda");
	if (hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
		printf("Device type cuda is not supported.\n");
		printf("Available device types:");
		while((hwDeviceType = av_hwdevice_iterate_types(hwDeviceType)) != AV_HWDEVICE_TYPE_NONE) {
			printf(" %s", av_hwdevice_get_type_name(hwDeviceType));
		}
		printf("\n");
		exit(1);
	}
	// Load the codec
	avcodec_register_all();
	codecName = "h264_nvenc";
	codec = avcodec_find_encoder_by_name(codecName);
	if (!codec) {
		printf("Codec h264_nvenc not found\n");
		exit(1);
	}

	int result;

    AVDictionary *dict = NULL;
    result = av_dict_set(&dict, "forced_idr", "1", 0);
    if( result < 0 ) {
		printf("Failed to create a dictionary entry for forced IDR\n");
		exit(1);
    }

	// Create the hardware context
	result = av_hwdevice_ctx_create(&hwDeviceCtx, hwDeviceType, NULL, dict, 0);
	if (result < 0) {
		printf("Failed to create HW device\n");
		exit(1);
	}

    if( dict ) av_dict_free( &dict );
}

/**
 * Initialize a context object for a given bitrate. This should occur after the hardware 
 * initialization.
 */
AVCodecContext* Hardware::initializeContext(int bitrateValue, int width, int height, int numTiles)
{
	// Allocate codec context. This is the struct with all of the parameters that
	// will be used when running the encoder, like bit rate, width/height, and GOP
	int ret;
	AVCodecContext* c = avcodec_alloc_context3(codec);
	if (!c) {
		printf("Error allocating video codec context\n");
		exit(1);
	}
	// Allocate space for the packet. NVENC will deposit the H264 bitstream into pkt.
	// To get the video, we just append all of the pkt's together.
	pkt = av_packet_alloc();
	if (!pkt) {
		printf("Error allocating packet\n");
		exit(1);
	}
	// Allocate space for the frame. This is a struct representing one frame of the
	// raw video. These are sent directly to NVENC.
	int initFrame = 0;
	if (frame == NULL) {
		initFrame = 1;
		frame = av_frame_alloc();
		if (!frame) {
			printf("Error allocating frame\n");
			exit(1);
		}
	}
	// Initialize GPU encoder
	c->get_format = getHwFormat;
	av_opt_set_int(c, "refcounted_frames", 1, 0);
	c->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

	// Encoding parameters
	// We multiply width by 2 because we encode two full videos, but the tile width will
	// only be the left or right half of the full video.
	//c->bit_rate = 400000; // TODO what is the equivalent for kvazaar?
	c->width = width;
	c->height = height;
	// FPS
	// TODO this is not giving us good timing values
	c->time_base = (AVRational){1, 25};
	// Image specifications
	//c->gop_size = state->encoder_control->cfg.gop_len; // TODO this comes up as only 4?
	// c->gop_size = 16;
	c->gop_size = 25;
	c->pix_fmt = AV_PIX_FMT_YUV420P;
	// Whether high or low bitrate
	c->bit_rate = bitrateValue;
	// Number of tiles == number of slices
	c->num_slices = numTiles;

	// Open the codec
	ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		printf("Error opening codec\n");
		exit(1);
	}
	// Open the frame
	if (initFrame) {
		frame->format = c->pix_fmt;
		frame->width = c->width;
		frame->height = c->height;
		ret = av_frame_get_buffer(frame, 32);
		if (ret < 0) {
			printf("Error allocating frame data\n");
			exit(1);
		}
	}
	// codecContextArr[bitrate].push_back(c);
    return c;
}

void Hardware::putImageInFrame( const unsigned char* y,
                                const unsigned char* u,
                                const unsigned char* v,
                                int yWidth,
                                int yHeight )
{
	int ret;
	ret = av_frame_make_writable(frame);
	if (ret < 0) {
		printf("Frame not writable\n");
		exit(1);
	}
	// Copy the source image to the frame to be encoded
	int uvWidth = yWidth / 2;
	for (int i=0; i<yHeight; i++) {
		memcpy(frame->data[0]+(i*yWidth), y+(i*yWidth), yWidth);
		if (i < yHeight / 2) {
			memcpy(frame->data[1]+(i*uvWidth), u+(i*uvWidth), uvWidth);
			memcpy(frame->data[2]+(i*uvWidth), v+(i*uvWidth), uvWidth);
		}
	}
	frame->pts = 0; // TODO do we need this?
}


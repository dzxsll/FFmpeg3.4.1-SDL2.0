/*
	JXY 12/25/2017
*/

#include<stdio.h>

#define __STDC_CONSTANT_MACROS  

#ifdef _WIN32
extern "C"
{
#include "libavdevice/avdevice.h" 
#include "libavformat/avformat.h"  
#include "libavcodec/avcodec.h"  
#include "libswscale/swscale.h" 
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "SDL/include/SDL.h"
};
#endif // WIN32

//For decode the input stream
AVFormatContext *pIFmtCtx;
AVCodecContext	*pDecoderCtx;
AVCodec			*pDecoder;
//For encode the output stream
AVFormatContext *pOFmtCtx;
AVCodecContext	*pEncoderCtx;
AVCodec			*pEncoder;
//For get the data and change the format of the data
int	     		 videoindex;
AVStream		*video_st;
AVFrame			*pFrame, *pFrameYUV;

struct SwsContext *img_convert_ctx;
struct SwsContext *img_convert_ctx1;

void Open_dshow_device(char *CameraName);

void Dispose();

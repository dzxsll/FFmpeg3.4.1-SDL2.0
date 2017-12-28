/*
	JXY 12/25/2017
*/

#include<stdio.h>

#define __STDC_CONSTANT_MACROS  

#ifdef _WIN32
extern "C"
{
#include "libavdevice/avdevice.h" 
#include "libavcodec/avcodec.h"  
#include "libavformat/avformat.h"  
#include "libswscale/swscale.h" 
#include "libavutil/imgutils.h"
#include "SDL/include/SDL.h"
};
#endif // WIN32

AVFormatContext *pFormatCtx;
int				videoindex;
AVCodecContext	*pCodecCtx;
AVCodec			*pCodec;
AVFrame			*pFrame, *pFrameYUV;
struct SwsContext *img_convert_ctx;

void Open_dshow_device(char *CameraName);

void Get_device_data();
#include "Functions.h"

//Refresh Event  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)  

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)  

SDL_Window *screen;
SDL_Renderer *sdlRenderer;
SDL_Rect sdlRect;
SDL_Texture *sdlTexture;
SDL_Thread *video_tid;
SDL_Event event;

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void *opaque)
{
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(40);
	}

	thread_exit = 0;
	thread_pause = 0;
	//break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

void Open_dshow_device(char *CameraName)
{
	/*av_register_all();*/
	avdevice_register_all();
	avcodec_register_all();
	avformat_network_init();

	char Order[50];
	snprintf(Order, sizeof(Order), "%s%s", "video=", CameraName);

	pFormatCtx = avformat_alloc_context();
	AVInputFormat *ifmt = av_find_input_format("dshow");
	if (avformat_open_input(&pFormatCtx, Order, ifmt, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return;
	}
}

void Get_device_data()
{
	//找到视频数据
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find steam information.\n");
		return;
	}

	//找到数据的第一帧
	videoindex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1) {
		printf("Couldn't find a video stream.\n");
		return;
	}

	//获取解码器
	pCodecCtx = avcodec_alloc_context3(NULL);
	if (pCodecCtx == NULL)
		return;
	avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar);
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {	//打开解码器
		printf("Couldn't open Codec.\n");
		return;
	}

	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	unsigned char *out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
}

void init_SDL()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("Couldn't initialize SLD _ %s\n", SDL_GetError());
		return;
	}

	int screen_w = 0, screen_h = 0;
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	screen = SDL_CreateWindow("SDLPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen)
	{
		printf("SDL : Couldn't create window - exiting:%s\n", SDL_GetError());
		return;
	}

	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
}

void playFrame()
{
	//读取数据
	int ret;
	AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));

	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

	for (;;) {
		//Wait
		SDL_WaitEvent(&event);

		if (event.type == SFM_REFRESH_EVENT) {
			while (1) {
				if (av_read_frame(pFormatCtx, packet) < 0)
					thread_exit = 1;
				if (packet->stream_index == videoindex)
					break;
			}
			ret = avcodec_send_packet(pCodecCtx, packet);
			if (ret != 0) {
				printf("Decode Error.\n");
				return;
			}
			while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);
			}
			av_packet_unref(packet);
		}
		else if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.sym == SDLK_SPACE)
				thread_pause = !thread_pause;
		}
		else if (event.type == SDL_QUIT) {
			thread_exit = 1;
		}
		else if (event.type == SFM_BREAK_EVENT) {
			break;
		}
	}
}

int main(int agrc, char *argv[])
{
	Open_dshow_device("WDM 2860 Capture");

	Get_device_data();

	init_SDL();

	playFrame();

	return 0;
}
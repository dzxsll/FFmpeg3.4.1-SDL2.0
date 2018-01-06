#include "Functions.h"
#include <Windows.h>

//Refresh Event  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)  
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)  

//Eevent Flag
int thread_exit = 0;
int thread_pause = 0;

//SDL
SDL_Window		*screen;
SDL_Renderer	*sdlRenderer;
SDL_Rect		sdlRect;
SDL_Texture		*sdlTexture;
SDL_Thread		*video_tid;
SDL_Event		event;

void play(AVFormatContext *pIFmtCtx, AVCodecContext *pDecoderCtx);

#pragma region Other

void show_dshow_device() {
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options, "list_devices", "true", 0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	printf("Device Info=============\n");
	avformat_open_input(&pFormatCtx, "video=dummy", iformat, &options);
	printf("========================\n");
}

int flush_encoder(AVFormatContext * fmt_ctx, unsigned int stream_index, int framecnt) {
	int ret;
	AVPacket enc_pkt;

	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);

		ret = avcodec_send_frame(pEncoderCtx, pFrame);
		while (avcodec_receive_packet(pEncoderCtx, &enc_pkt) == 0) {
			printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);

			AVRational time_base = fmt_ctx->streams[stream_index]->time_base;
			AVRational r_frameratel = pIFmtCtx->streams[stream_index]->r_frame_rate;
			AVRational time_base_q = { 1,AV_TIME_BASE };

			int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_frameratel));

			enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
			enc_pkt.dts = enc_pkt.pts;
			enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);

			enc_pkt.pos = -1;
			framecnt++;
			fmt_ctx->duration = enc_pkt.duration*framecnt;

			ret = av_interleaved_write_frame(fmt_ctx, &enc_pkt);
			if (ret < 0)
				break;
		}
	}
	return ret;
}

void Dispose()
{
	avformat_close_input(&pIFmtCtx);
	avformat_close_input(&pOFmtCtx);
	avio_close(pOFmtCtx->pb);
	avformat_free_context(pIFmtCtx);
	avformat_free_context(pOFmtCtx);
	avcodec_free_context(&pDecoderCtx);
	avcodec_free_context(&pEncoderCtx);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);
}

#pragma endregion

#pragma region Initialize

void Register_all_needed()
{
	//The function should be ran at frist.
	//If don't,can't get what we needed.
	av_register_all();
	avdevice_register_all();
	avcodec_register_all();
}

void Open_dshow_device(char *CameraName)
{
	//Get the command string.
	char Order[50];
	snprintf(Order, sizeof(Order), "%s%s", "video=", CameraName);

	//Find the DirectShow devices
	AVInputFormat *ifmt = av_find_input_format("dshow");

	//Open the device
	if (avformat_open_input(&pIFmtCtx, Order, ifmt, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return;
	}
}

void Input_initialize()
{
	//Find the stream data from the Device
	if (avformat_find_stream_info(pIFmtCtx, NULL) < 0) {
		printf("Couldn't find steam information.\n");
		return;
	}

	//Check the Vidoe stream 
	videoindex = -1;
	for (int i = 0; i < pIFmtCtx->nb_streams; i++) {
		if (pIFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1) {
		printf("Couldn't find a video stream.\n");
		return;
	}

	//Get the decoderContext.
	pDecoderCtx = avcodec_alloc_context3(NULL);
	if (pDecoderCtx == NULL)
		return;
	avcodec_parameters_to_context(pDecoderCtx, pIFmtCtx->streams[videoindex]->codecpar);

	//Get the decoder.
	pDecoder = avcodec_find_decoder(pDecoderCtx->codec_id);
	if (pDecoder == NULL) {
		printf("Codec not found.\n");
		return;
	}

	//Open the decoder.
	if (avcodec_open2(pDecoderCtx, pDecoder, NULL) < 0) {	//Open the Codec
		printf("Couldn't open Codec.\n");
		return;
	}

	//The containers for origin frame and converted frame
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	//The format of the frame should be converted. 
	img_convert_ctx = sws_getContext(
		pDecoderCtx->width, pDecoderCtx->height, pDecoderCtx->pix_fmt,
		pDecoderCtx->width, pDecoderCtx->height, AV_PIX_FMT_YUV420P,
		SWS_BICUBIC, NULL, NULL, NULL);

	//Initialize the pFrameYUV 
	unsigned char *out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pDecoderCtx->width, pDecoderCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pDecoderCtx->width, pDecoderCtx->height, 1);
}

void Output_initialize()
{
	avformat_network_init();

	//The URL fo server
	char *out_filename = "rtmp://127.0.0.1/oflaDemo/streams";
	/*char *out_filename = "rtp://127.0.0.1:6666";*/

	//The FFmpeg treat the RTMP protocol as file.			
	avformat_alloc_output_context2(&pOFmtCtx, NULL, "flv", out_filename);
	//avformat_alloc_output_context2(&pOFmtCtx, NULL, "mpegts", out_filename);//UDP
	//pOFmtCtx->oformat = av_guess_format("rtp", NULL, NULL);

	//Initialize encoder
	pEncoder = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pEncoder) {
		printf("Can not find encoder!");
		return;
	}
	pEncoderCtx = avcodec_alloc_context3(pEncoder);
	pEncoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pEncoderCtx->width = pIFmtCtx->streams[videoindex]->codecpar->width;
	pEncoderCtx->height = pIFmtCtx->streams[videoindex]->codecpar->height;
	pEncoderCtx->time_base.num = 1;
	pEncoderCtx->time_base.den = 25;
	pEncoderCtx->bit_rate = 400 * 1000;

	if (pOFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
		pEncoderCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

	//H264 codec param  
	//pCodecCtx->me_range = 16;  
	//pCodecCtx->max_qdiff = 4;  
	//pCodecCtx->qcompress = 0.6;  
	pEncoderCtx->qmin = 10;
	pEncoderCtx->qmax = 51;
	//Optional Param
	pEncoderCtx->max_b_frames = 3;
	//Set H264 preset and tune
	AVDictionary *param = 0;
	av_dict_set(&param, "preset", "fast", 0);
	av_dict_set(&param, "tune", "zerolatency", 0);

	if (avcodec_open2(pEncoderCtx, pEncoder, &param)) {
		printf("Failed to open encoder! ");
		return;
	}

	//Add a new stream to ouput,should be called by the user before avformat_write_header
	video_st = avformat_new_stream(pOFmtCtx, pEncoder);
	if (video_st == NULL) {
		return;
	}
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;
	avcodec_parameters_from_context(video_st->codecpar, pEncoderCtx);

	if (avio_open(&pOFmtCtx->pb, out_filename, AVIO_FLAG_READ_WRITE)) {
		printf("Failed to open output file!");
		return;
	}

	av_dump_format(pOFmtCtx, 0, out_filename, 1);

	avformat_write_header(pOFmtCtx, NULL);
}

#pragma endregion

#pragma region Functions

void Encode_and_push()
{
	int ret, framecnt = 0;

	int64_t start_time = av_gettime();

	AVPacket enc_pkt;
	enc_pkt.data = NULL;
	enc_pkt.size = 0;
	av_init_packet(&enc_pkt);

	AVPacket *dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));

	//Get the data of camera
	while (av_read_frame(pIFmtCtx, dec_pkt) >= 0) {
		if (thread_exit)
			break;

		pFrame = av_frame_alloc();
		if (!pFrame) {
			printf("Failed to alloc a frame!");
			return;
		}

		//Decode the data 
		ret = avcodec_send_packet(pDecoderCtx, dec_pkt);
		if (ret != 0) {
			printf("Decode Error.\n");
			return;
		}
		while (avcodec_receive_frame(pDecoderCtx, pFrame) == 0) {
			sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pDecoderCtx->height, pFrameYUV->data, pFrameYUV->linesize);
			pFrameYUV->format = AV_PIX_FMT_YUV420P;
			pFrameYUV->width = pDecoderCtx->width;
			pFrameYUV->height = pDecoderCtx->height;

			//Encode the data to H264
			ret = avcodec_send_frame(pEncoderCtx, pFrameYUV);
			if (ret != 0) {
				printf("Encode Error.\n");
				return;
			}
			while (avcodec_receive_packet(pEncoderCtx, &enc_pkt) == 0) {
				printf("Succeed to encode frame: %5d\tsize:%5d\n", framecnt, enc_pkt.size);
				framecnt++;
				enc_pkt.stream_index = video_st->index;

				//Add the pts and dts
				AVRational time_base = pOFmtCtx->streams[videoindex]->time_base;
				AVRational r_frameratel = pIFmtCtx->streams[videoindex]->r_frame_rate;
				AVRational time_base_q = { 1,AV_TIME_BASE };

				/*Duration between 2 frames (us).
				According to streams's r_frame_rate, the time between 2 frames is
				1/(av_q2d(r_frameratel)) but we should use the ffmpeg time as standard time.
				So AV_TIME_BASE is needed; #define AV_TIME_BASE 1000000
				*/
				int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_frameratel));

				/*The timebase is different in the FFmepeg container.
				so change the inputStream timebase to outputStream timebase.
				*/
				enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
				enc_pkt.dts = enc_pkt.pts;
				enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
				enc_pkt.pos = 1;

				//Delay
				int64_t pts_time = av_rescale_q(enc_pkt.dts, time_base, time_base_q);
				int64_t now_time = av_gettime() - start_time;
				if (pts_time > now_time)
					av_usleep(pts_time - now_time);

				//Send the data
				ret = av_interleaved_write_frame(pOFmtCtx, &enc_pkt);
				av_packet_unref(&enc_pkt);
			}
			av_frame_unref(pFrame);
		}

		av_packet_unref(dec_pkt);
	}

	//Flush Encoder
	ret = flush_encoder(pOFmtCtx, 0, framecnt);
	if (ret < 0) {
		printf("Flushing encoder failed\n");
		return;
	}

	av_write_trailer(pOFmtCtx);

	Dispose();
}

void GetData_and_decode(char *in_filename)
{
	AVFormatContext	*pFmtCtx;
	AVCodecContext	*pCodecCtx;
	AVCodec			*pCodec;
	int				video_index, frame_index;

	//init the container
	Register_all_needed();
	pFmtCtx = avformat_alloc_context();
	while (avformat_open_input(&pFmtCtx, in_filename, 0, 0) < 0) {
		pFmtCtx = avformat_alloc_context();
		return;
	}
	if (avformat_find_stream_info(pFmtCtx, 0) < 0) {
		return;
	}
	for (int i = 0; i < pFmtCtx->nb_streams; i++) {
		if (pFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_index = i;
			break;
		}
	}

	pCodecCtx = avcodec_alloc_context3(NULL);
	if (pCodecCtx == NULL)
		return;
	avcodec_parameters_to_context(pCodecCtx, pFmtCtx->streams[video_index]->codecpar);

	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return;
	}

	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {	//Open the Codec
		printf("Couldn't open Codec.\n");
		return;
	}

	/*AVFormatContext	*pOutFmtCtx;
	AVCodecContext	*pOutCodecCtx;
	AVCodec			*pOutCodec;

	AVPacket *pOutPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
	AVFrame	 *pOutFrame = av_frame_alloc();
*/
	img_convert_ctx1 = sws_getContext(
		pDecoderCtx->width, pDecoderCtx->height, pCodecCtx->pix_fmt,
		pDecoderCtx->width, pDecoderCtx->height, AV_PIX_FMT_YUV420P,
		SWS_BICUBIC, NULL, NULL, NULL);

	//decode the data and play	
	play(pFmtCtx, pCodecCtx);
}

#pragma endregion

#pragma region Play

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

void init_SDL(AVCodecContext *pDecoderCtx)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("Couldn't initialize SLD _ %s\n", SDL_GetError());
		return;
	}

	int screen_w = 0, screen_h = 0;
	screen_w = pDecoderCtx->width;
	screen_h = pDecoderCtx->height;
	screen = SDL_CreateWindow("SDLPlayer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen)
	{
		printf("SDL : Couldn't create window - exiting:%s\n", SDL_GetError());
		return;
	}

	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pDecoderCtx->width, pDecoderCtx->height);

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
}

void play(AVFormatContext *pIFmtCtx, AVCodecContext *pDecoderCtx)
{
	//Create the frame window.
	init_SDL(pDecoderCtx);

	AVPacket	*packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	AVFrame		*pframe, *pframYUV;
	int ret;

	pframe = av_frame_alloc();
	pframYUV = av_frame_alloc();
	unsigned char *out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pDecoderCtx->width, pDecoderCtx->height, 1));
	av_image_fill_arrays(pframYUV->data, pframYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, pDecoderCtx->width, pDecoderCtx->height, 1);

	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

	for (;;) {
		//Wait
		SDL_WaitEvent(&event);

		if (event.type == SFM_REFRESH_EVENT) {
			//Get the frame data.
			while (1) {
				if (av_read_frame(pIFmtCtx, packet) < 0)
					thread_exit = 1;
				if (packet->stream_index == videoindex)
					break;
			}
			//Decode the data.
			ret = avcodec_send_packet(pDecoderCtx, packet);
			if (ret != 0) {
				printf("Decode Error.\n");
				return;
			}
			//Recevie the decoded frame.
			if (avcodec_receive_frame(pDecoderCtx, pframe) == 0) {
				sws_scale(img_convert_ctx1, (const unsigned char* const*)pframe->data, pframe->linesize, 0, pDecoderCtx->height, pframYUV->data, pframYUV->linesize);

				//Get the full picture and show that
				/*AVFrame *pDstFrame = av_frame_alloc();
				uint8_t *dstbuf = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pDecoderCtx->width * 2, pDecoderCtx->height, 1));
				av_image_fill_arrays(pDstFrame->data, pDstFrame->linesize, dstbuf, AV_PIX_FMT_YUV420P, pFrame->width * 2, pFrame->height, 1);*/

				SDL_UpdateTexture(sdlTexture, NULL, pframYUV->data[0], pframYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);						//Show the image.
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

#pragma endregion

DWORD WINAPI pushData(LPVOID pM)
{
	Encode_and_push();

	return 0;
}

int main(int agrc, char *argv[])
{
	Register_all_needed();

	//Open_dshow_device("Integrated Webcam");
	Open_dshow_device("WDM 2860 Capture");

	Input_initialize();

	Output_initialize();

	//Encode_and_push();

	HANDLE handle = CreateThread(NULL, 0, pushData, NULL, 0, NULL);

	GetData_and_decode("rtmp://127.0.0.1/oflaDemo/streams");

	//playFrame(pIFmtCtx, pDecoderCtx);

	return 0;
}
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

void Dispose();

int flush_encoder(AVFormatContext * fmt_ctx, unsigned int stream_index, int framecnt);

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

void Register_all_needed()
{
	//The function should be ran at frist.
	//If don't , can't get what we needed.
	av_register_all();
	avdevice_register_all();
	avcodec_register_all();
}

void Open_dshow_device(char *CameraName)
{
	char Order[50];
	snprintf(Order, sizeof(Order), "%s%s", "video=", CameraName);

	pIFmtCtx = avformat_alloc_context();
	AVInputFormat *ifmt = av_find_input_format("dshow");				//find the DirectShow devices
	if (avformat_open_input(&pIFmtCtx, Order, ifmt, NULL) != 0) {		//open the device
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

	//Get the decoder.
	pDecoderCtx = avcodec_alloc_context3(NULL);
	if (pDecoderCtx == NULL)
		return;
	avcodec_parameters_to_context(pDecoderCtx, pIFmtCtx->streams[videoindex]->codecpar);
	pDecoder = avcodec_find_decoder(pDecoderCtx->codec_id);
	if (pDecoderCtx == NULL) {
		printf("Codec not found.\n");
		return;
	}
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

	char *out_filename = "rtp://localhost:6666";

	//The FFmpeg treat the RTMP protocol as file.
	//avformat_alloc_output_context2(&pOFmtCtx, NULL, "flv", out_filename);
	AVOutputFormat *oformat = av_guess_format("rtp", NULL, NULL);
	avformat_alloc_output_context2(&pOFmtCtx, oformat, "mpegts", out_filename);//UDP  

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

void Coedc_and_push()
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

				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(sdlRenderer);
				SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
				SDL_RenderPresent(sdlRenderer);						//Show the image.

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

void init_SDL()
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

void playFrame()
{
	int ret;
	AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));

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
			while (avcodec_receive_frame(pDecoderCtx, pFrame) == 0) {
				sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pDecoderCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
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

void Dispose()
{
	avio_close(pOFmtCtx->pb);
	avformat_free_context(pIFmtCtx);
	avformat_free_context(pOFmtCtx);
	avcodec_free_context(&pDecoderCtx);
	avcodec_free_context(&pEncoderCtx);
	av_frame_free(&pFrame);
	av_frame_free(&pFrameYUV);
}

int main(int agrc, char *argv[])
{
	Register_all_needed();

	Open_dshow_device("WDM 2860 Capture");

	Input_initialize();

	Output_initialize();

	init_SDL();

	Coedc_and_push();

	playFrame();

	return 0;
}
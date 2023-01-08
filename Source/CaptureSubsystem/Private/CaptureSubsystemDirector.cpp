// Fill out your copyright notice in the Description page of Project Settings.
#include "CaptureSubsystemDirector.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#if WITH_EDITOR
#include "Editor.h"
#endif
#include "CaptureGameViewportClient.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "EncoderThread.h"
#include "VideoCaptureSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"


UCaptureSubsystemDirector::UCaptureSubsystemDirector() :
	Outputs(nullptr),
	Inputs(nullptr),
	FilterGraph(nullptr),
	BufferSinkContext(nullptr),
	BufferSrcContext(nullptr),
	
	GameWindow(nullptr),
	BuffBgr(nullptr),
	Runnable(nullptr),
	RunnableThread(nullptr),
	OutFormatContext(nullptr),
	VideoEncoderCodecContext(nullptr),
	AudioEncoderCodecContext(nullptr),
	SwsContext(nullptr),
	OutVideoStream(nullptr),
	OutAudioStream(nullptr),
	SWRContext(nullptr),
	AudioFrame(nullptr),
	VideoFrame(nullptr)
{
	OutputChannels[0] = nullptr;
	OutputChannels[1] = nullptr;
	
}

UCaptureSubsystemDirector::~UCaptureSubsystemDirector()
{

}

void UCaptureSubsystemDirector::DestoryDirector()
{
	if (!IsDestroy)
	{
		
		if (FAudioDeviceHandle AudioDeviceHandle = GEngine->GetMainAudioDevice())
		{
		
	
			AudioDeviceHandle->UnregisterSubmixBufferListener(this);
		}
		
		RunnableThread->Kill(true);
		delete Runnable;
		Runnable = nullptr;

		
		
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
		
		
		
		

#if WITH_EDITOR
		FEditorDelegates::PrePIEEnded.Remove(EndPIEDelegateHandle);
#endif
		FTSTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);

		Encode_Finish();
		FMemory::Free(OutputChannels[0]);
		FMemory::Free(OutputChannels[1]);
		FMemory::Free(BuffBgr);
		IsDestroy = true;
	}
}

void UCaptureSubsystemDirector::EndWindowReader(const bool i)
{
	DestoryDirector();
	this->RemoveFromRoot();
	this->ConditionalBeginDestroy();
	this->BeginDestroy();
}

void UCaptureSubsystemDirector::EndWindowReader_StandardGame(void* i)
{
	DestoryDirector();
	this->RemoveFromRoot();
	this->ConditionalBeginDestroy();
	this->BeginDestroy();
}

void UCaptureSubsystemDirector::Begin_Receive_AudioData(UWorld* World)
{
	GameMode = World->WorldType;


	if (FAudioDeviceHandle AudioDeviceHandle = GEngine->GetMainAudioDevice())
	{
		
	
		AudioDeviceHandle->RegisterSubmixBufferListener(this);
	}
}

void UCaptureSubsystemDirector::Initialize_Director(UWorld* World,FVideoCaptureOptions CaptureOptions)
{
	
	avfilter_register_all();
	//avfilter_register()
	av_register_all();
	avformat_network_init();
	Options=CaptureOptions;
	
	
	VideoTickTime = static_cast<float>(1) / static_cast<float>(Options.FPS);
	

	GameWindow = GEngine->GameViewport->GetWindow().Get();

	
	OutWidth  = FormatSize_X(GEngine->GameViewport->Viewport->GetSizeXY().X);
	OutHeight  = GEngine->GameViewport->Viewport->GetSizeXY().Y;
	
	const int Crop=Options.OptionalCaptureAspectRatio.IsZero()?OutWidth: (OutHeight*Options.OptionalCaptureAspectRatio.X)/Options.OptionalCaptureAspectRatio.Y;
	OutWidth=Crop;
	
	
	OutputChannels[0] = static_cast<uint8_t*>(FMemory::Realloc(OutputChannels[0], 4096));
	OutputChannels[1] = static_cast<uint8_t*>(FMemory::Realloc(OutputChannels[1], 4096));

	FString Scale;
	FString Resolution;

	FString Str_width;
	FString Str_height;
	if (Options.VideoFilter.Len() > 0)
	{
		Options.VideoFilter.Split("=", &Scale, &Resolution);
		Resolution.Split(":", &Str_width, &Str_height);
		OutWidth= FCString::Atoi(*Str_width);
		OutHeight= FCString::Atoi(*Str_height);
	}
	FilterDescription.Append("[in]");
	FilterDescription.Append("scale=");
	FilterDescription.Append(FString::FromInt(OutWidth));
	FilterDescription.Append(":");
	FilterDescription.Append(FString::FromInt(OutHeight));
	FilterDescription.Append("[out]");
	

	int IsUseRTMP = Options.OutFileName.Find("rtmp");
	if (IsUseRTMP==0)
	{
		if (avformat_alloc_output_context2(&OutFormatContext, NULL, "flv", TCHAR_TO_ANSI(*(Options.OutFileName))) < 0)
		{
			check(false);
		}
	}
	else
	{
		if (avformat_alloc_output_context2(&OutFormatContext, NULL, NULL, TCHAR_TO_ANSI(*(Options.OutFileName))) < 0)
		{
			UE_LOG(LogCaptureSubsystem,Fatal,TEXT("potential File name error %s"),*Options.OutFileName)

		}
	}
	//create audio encoder
	//Create_Audio_Swr();
	Create_Audio_Encoder("aac");

	//create video encoder



	

	//bind delegate for get video data and audio data 
	Begin_Receive_VideoData();
	
	
	Begin_Receive_AudioData(World);

	//End PIE deleate and tick delegate
	AddEndFunction();
	AddTickFunction();
}

void UCaptureSubsystemDirector::Begin_Receive_VideoData()
{
	
	FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &UCaptureSubsystemDirector::OnBackBufferReady_RenderThread);
}

void UCaptureSubsystemDirector::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
	
	if (GameWindow == &SlateWindow)
	{
		if (TickTime >= VideoTickTime)
		{
			UCaptureGameViewportClient* ViewportClient =static_cast<UCaptureGameViewportClient*>( GetWorld()->GetGameViewport());
			auto Texture=ViewportClient->MyRenderTarget->GetResource()->GetTexture2DRHI();
			
			GameTexture =Options.ShowUI?BackBuffer:Texture;
			TickTime -= VideoTickTime;
			if(!Runnable)
			{
				CreateEncodeThread();
				Alloc_Video_Filter();
			}
			GetScreenVideoData();		
		}
	}
}

bool UCaptureSubsystemDirector::AddTickTime(float time)
{
	TickTime += time;

	return true;
}

void UCaptureSubsystemDirector::AddEndFunction()
{
	if(GameMode== EWorldType::Game)
		FSlateApplication::Get().GetRenderer()->OnSlateWindowDestroyed().AddUObject(this, &UCaptureSubsystemDirector::EndWindowReader_StandardGame);
#if WITH_EDITOR
	if(GameMode == EWorldType::PIE)
		FEditorDelegates::EndPIE.AddUObject(this, &UCaptureSubsystemDirector::EndWindowReader);
#endif
}

void UCaptureSubsystemDirector::AddTickFunction()
{
	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCaptureSubsystemDirector::AddTickTime));
}

void UCaptureSubsystemDirector::GetScreenVideoData()
{
	
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
	
	SCOPED_DRAW_EVENT(RHICmdList, CaptureEvent);
	
	RHICmdList.TransitionResource(ERHIAccess::ReadOnlyMask,GameTexture);
	
	TextureData = (uint8*)RHICmdList.LockTexture2D(GameTexture->GetTexture2D(), 0, EResourceLockMode::RLM_ReadOnly, TextureStride, false);
	if(Runnable)
		Runnable->InsertVideo(TextureData);
	RHICmdList.UnlockTexture2D(GameTexture, 0, false);
	
	

}

void UCaptureSubsystemDirector::CreateEncodeThread()
{
	Create_Video_Encoder(Options.UseGPU, TCHAR_TO_ANSI(*Options.OutFileName), Options.VideoBitRate);
	
	Runnable = new FEncoderThread();
	Runnable->CreateQueue(sizeof(FColor) * GameTexture->GetSizeX()*GameTexture->GetSizeY(), 30);
	BuffBgr = static_cast<uint8_t*>(FMemory::Realloc(BuffBgr, 3 * GameTexture->GetSizeX() * GameTexture->GetSizeY()));
	Runnable->video_encode_delegate.BindUObject(this, &UCaptureSubsystemDirector::Encode_Video_Frame);
	
	RunnableThread = FRunnableThread::Create(Runnable, TEXT("EncoderThread"));
	
	
}

void UCaptureSubsystemDirector::Create_Audio_Encoder(const char* audioencoder_name)
{
	AVCodec* audioencoder_codec;
	audioencoder_codec = avcodec_find_encoder_by_name(audioencoder_name);
	OutAudioStream = avformat_new_stream(OutFormatContext, audioencoder_codec);
	AudioIndex = OutAudioStream->index;
	AudioEncoderCodecContext = avcodec_alloc_context3(audioencoder_codec);

	if (!OutAudioStream)
	{
		check(false);
	}
	AudioEncoderCodecContext->codec_id = AV_CODEC_ID_AAC;
	AudioEncoderCodecContext->bit_rate = 120000;
	AudioEncoderCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
	AudioEncoderCodecContext->sample_rate = 48000;
	AudioEncoderCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
	AudioEncoderCodecContext->channels = 2;
	AudioEncoderCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;

	if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		AudioEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	AudioEncoderCodecContext->codec_tag = 0;
	OutAudioStream->codecpar->codec_tag = 0;

	if (avcodec_open2(AudioEncoderCodecContext, audioencoder_codec, NULL) < 0)
	{
		check(false);
	}
	avcodec_parameters_from_context(OutAudioStream->codecpar, AudioEncoderCodecContext);

	AudioFrame = av_frame_alloc();
	AudioFrame->nb_samples = AudioEncoderCodecContext->frame_size;
	AudioFrame->format = AudioEncoderCodecContext->sample_fmt;
}

void UCaptureSubsystemDirector::Create_Video_Encoder(bool is_use_NGPU, const char* out_file_name, int bit_rate)
{
	AVCodec *encoder_codec;
	int ret;

	if (is_use_NGPU)
	{
		encoder_codec = avcodec_find_encoder_by_name("nvenc_h264");
	}
	else
	{
		encoder_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!encoder_codec)
	{
		check(false);
	}
	OutVideoStream = avformat_new_stream(OutFormatContext, encoder_codec);

	VideoIndex = OutVideoStream->index;

	VideoEncoderCodecContext = avcodec_alloc_context3(encoder_codec);
	if (!VideoEncoderCodecContext)
	{
		check(false);
	}
	VideoEncoderCodecContext->bit_rate = bit_rate;
	//video_encoder_codec_context->rc_min_rate = bit_rate;
	//video_encoder_codec_context->rc_max_rate = bit_rate;
	//video_encoder_codec_context->bit_rate_tolerance = bit_rate;
	//video_encoder_codec_context->rc_buffer_size = bit_rate;
	//video_encoder_codec_context->rc_initial_buffer_occupancy = bit_rate * 3 / 4;
	
	VideoEncoderCodecContext->width = OutWidth;
	VideoEncoderCodecContext->height = OutHeight;
	VideoEncoderCodecContext->max_b_frames = 2;
	VideoEncoderCodecContext->time_base.num = 1;
	VideoEncoderCodecContext->time_base.den = Options.FPS;
	VideoEncoderCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	VideoEncoderCodecContext->me_range = 16;
	VideoEncoderCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
	VideoEncoderCodecContext->profile = FF_PROFILE_H264_BASELINE;
	VideoEncoderCodecContext->frame_number = 1;
	VideoEncoderCodecContext->qcompress = 0.8;
	VideoEncoderCodecContext->max_qdiff = 4;
	VideoEncoderCodecContext->level = 30;
	VideoEncoderCodecContext->gop_size = 25;
	VideoEncoderCodecContext->qmin = 18;
	VideoEncoderCodecContext->qmax = 28;
	VideoEncoderCodecContext->me_range = 16;
	VideoEncoderCodecContext->framerate = { Options.FPS,1 };

	//ultrafast,superfast, veryfast, faster, fast, medium, slow, slower, veryslow,placebo.
	if (encoder_codec)
		av_opt_set(VideoEncoderCodecContext->priv_data, "preset", "fast",
			0);

	if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		VideoEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	
	int i = avcodec_open2(VideoEncoderCodecContext, encoder_codec, NULL);
	if (i < 0)
	{
		LogErrorUE(i,true);
		check(false);
	}

	ret = avcodec_parameters_from_context(OutVideoStream->codecpar, VideoEncoderCodecContext);
	if (ret < 0)
	{
		check(false);
	}

	VideoFrame = av_frame_alloc();
	if (!VideoFrame)
	{
		check(false);
	}

	avio_open(&OutFormatContext->pb, out_file_name, AVIO_FLAG_WRITE);
	ret = av_image_alloc(
		VideoFrame->data,
		VideoFrame->linesize,
		OutWidth,
		OutHeight,
		VideoEncoderCodecContext->pix_fmt,
		32);
	if (ret < 0)
	{
		check(false);
	}

	const int Crop=Options.OptionalCaptureAspectRatio.IsZero()?GameTexture->GetSizeX(): (GameTexture->GetSizeY()*Options.OptionalCaptureAspectRatio.X)/Options.OptionalCaptureAspectRatio.Y;

	SwsContext = sws_getCachedContext(SwsContext,
		Crop, GameTexture->GetSizeY(), AV_PIX_FMT_BGR24,
		OutWidth, OutHeight, AV_PIX_FMT_YUV420P,
		SWS_FAST_BILINEAR, 0, 0, 0);

	if (avformat_write_header(OutFormatContext, NULL) < 0)
	{
		check(false);
	}

	Video_Frame_Duration = OutVideoStream->time_base.den / Options.FPS;
}

void UCaptureSubsystemDirector::Video_Frame_YUV_From_BGR(uint8_t *rgb,uint32 LineSize)
{
	

	const int In_Linesize[1] = {static_cast<int>( 3 * LineSize )};
	sws_scale(SwsContext, (const uint8_t * const *)&rgb, In_Linesize, 0,
		GameTexture->GetSizeY(), VideoFrame->data, VideoFrame->linesize);
	
	VideoFrame->width = OutWidth;
	VideoFrame->height = OutHeight;
	VideoFrame->format = AV_PIX_FMT_YUV420P;
	//Calculate aspect ratio
	
}

void UCaptureSubsystemDirector::Create_Audio_Swr(int NumChannels)
{
	SWRContext = swr_alloc();
	
	av_opt_set_int(SWRContext, "in_channel_layout",  NumChannels==2?AV_CH_LAYOUT_STEREO:AV_CH_LAYOUT_7POINT1, 0);
	av_opt_set_int(SWRContext, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
	av_opt_set_int(SWRContext, "in_sample_rate", 48000, 0);
	av_opt_set_int(SWRContext, "out_sample_rate", 48000, 0);
	av_opt_set_sample_fmt(SWRContext, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
	av_opt_set_sample_fmt(SWRContext, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
	swr_init(SWRContext);
}

void UCaptureSubsystemDirector::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{

	if(!Runnable)
		return;
	if(!Runnable->IsAudioThreadInitialized())
	{
	
		Runnable->CreateAudioQueue( NumSamples * sizeof(float),40);
		Runnable->GetAudioProcessDelegate().BindUObject(this, &UCaptureSubsystemDirector::Encode_Audio_Frame);
		Runnable->GetAudioTimeProcessDelegate().BindUObject(this, &UCaptureSubsystemDirector::Encode_SetCurrentAudioTime);
	}
	if(!SWRContext)
	{
		Create_Audio_Swr(NumChannels);
	}
	
		if(InitialAudioTime<0)
		{
			InitialAudioTime=AudioClock;
		}
		AudioClock=AudioClock-InitialAudioTime;
		Runnable->InsertAudio((uint8_t*)AudioData, (uint8_t*)&AudioClock);
	
}

void UCaptureSubsystemDirector::Encode_Audio_Frame(uint8_t *rgb)
{
	const uint8_t* data = rgb;
	AVPacket* audio_pkt = av_packet_alloc();
	av_init_packet(audio_pkt);
	
	int count = swr_convert(SWRContext, OutputChannels, 4096, &data, 1024);
	int got_output = 0;

	AudioFrame->data[0] = OutputChannels[0];
	AudioFrame->data[1] = OutputChannels[1];
	AudioFrame->nb_samples=count;
	Set_Audio_Volume(AudioFrame);

	
	if (const auto ErrorNum=avcodec_encode_audio2(AudioEncoderCodecContext,audio_pkt,AudioFrame,&got_output)<0 )
	{
		LogErrorUE(ErrorNum,true);
		
	}
	if (got_output)
	{
		audio_pkt->pts = audio_pkt->dts = av_rescale_q(
			(CurrentAudioTime + Options.AudioDelay) / av_q2d({ 1,48000 }),
			{ 1,48000 },
			OutAudioStream->time_base);
		
		audio_pkt->duration = av_rescale_q(
			audio_pkt->duration,
			{ 1,48000 },
			OutAudioStream->time_base);

		audio_pkt->stream_index = AudioIndex;
		av_write_frame(OutFormatContext, audio_pkt);
		av_packet_unref(audio_pkt);
	}
}

void UCaptureSubsystemDirector::Encode_Video_Frame(uint8_t *rgb)
{
	
	uint32 Row = 0;
	uint32 Col = 0;
	uint8* TextureDataPtr=rgb;
	uint8_t* Firset_ptr = BuffBgr;
	AVPacket* Video_Pkt = av_packet_alloc();
	const int Crop=Options.OptionalCaptureAspectRatio.IsZero()?GameTexture->GetSizeX(): (GameTexture->GetSizeY()*Options.OptionalCaptureAspectRatio.X)/Options.OptionalCaptureAspectRatio.Y;
	const uint32 Difference=GameTexture->GetSizeX()-Crop;
	av_init_packet(Video_Pkt);
	for (Row = 0; Row < GameTexture->GetSizeY(); ++Row)
	{
		
		uint32* PixelPtr = (uint32*)TextureDataPtr;
		for (Col = 0; Col < GameTexture->GetSizeX(); ++Col)
		{
			
		
			if(Col>=Difference/2 &&Col<=GameTexture->GetSizeX()-Difference/2){
			uint32 EncodedPixel = *PixelPtr;
			//	AV_PIX_FMT_BGR24	这里暂时转换为BGR
			//	AV_PIX_FMT_RGB24	掉帧严重 暂时不知道为什么
			*(BuffBgr + 2) = (EncodedPixel >> 2) & 0xFF;
			*(BuffBgr + 1) = (EncodedPixel >> 12) & 0xFF;
			*(BuffBgr) = (EncodedPixel >> 22) & 0xFF;
			BuffBgr += 3;
		}
			++PixelPtr;		
		}
		
		TextureDataPtr += TextureStride;
	}
	BuffBgr = Firset_ptr;

	int got_output = 0;
	//We need to pass actual calculated linesize because of roudnings of the int
	int ShiftStride=Difference!=0?1:0;
	Video_Frame_YUV_From_BGR(BuffBgr,ShiftStride+(GameTexture->GetSizeX()-Difference/2)-Difference/2 );
	VideoFrame->pts = Video_Pts;

	AVFrame* filt_frame = av_frame_alloc();

	if (av_buffersrc_add_frame_flags(BufferSrcContext, VideoFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
	{
		check(false);
	}
	while (1)
	{
		int ret = av_buffersink_get_frame(BufferSinkContext, filt_frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0)
			break;
		if (ret >= 0)
		{
			avcodec_send_frame(VideoEncoderCodecContext, filt_frame);
			while (ret >= 0)
			{
				ret = avcodec_receive_packet(VideoEncoderCodecContext, Video_Pkt);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				{
					av_packet_unref(Video_Pkt);
					break;
				}
				else if (ret < 0)
				{
					av_packet_unref(Video_Pkt);
					break;
				}
				Video_Pkt->stream_index = VideoIndex;
				Video_Pkt->pts = Video_Pkt->dts = VideoFrame->pts*Video_Frame_Duration;
				Video_Pkt->stream_index = VideoIndex;
				Video_Pkt->duration = Video_Frame_Duration;
				++Video_Pts;
				av_write_frame(OutFormatContext, Video_Pkt);
			}
			av_packet_unref(Video_Pkt);
		}
	}
	av_frame_unref(filt_frame);	
}

void UCaptureSubsystemDirector::Encode_SetCurrentAudioTime(uint8_t* rgb)
{
	CurrentAudioTime = *(double*)rgb;
	
}

void UCaptureSubsystemDirector::Set_Audio_Volume(AVFrame *frame) const
{
	float *Ch_Left = reinterpret_cast<float*>(frame->data[0]);
	float *Ch_Right = reinterpret_cast<float*>(frame->data[1]);
	for (int i = 0; i < frame->nb_samples; i++)
	{
		Ch_Left[i] *= Options.SoundVolume;
		Ch_Right[i] *= Options.SoundVolume;
	}
}


void UCaptureSubsystemDirector::Alloc_Video_Filter()
{
	Outputs = avfilter_inout_alloc();
	Inputs = avfilter_inout_alloc();
	const AVFilter *buffersrc = avfilter_get_by_name("buffer");
	const AVFilter *buffersink = avfilter_get_by_name("buffersink");
	enum AVPixelFormat pix_fmts[] = { VideoEncoderCodecContext->pix_fmt, AV_PIX_FMT_NONE };
	AVRational time_base = {1,1000000};

	FilterGraph = avfilter_graph_alloc();
	if (!Outputs || !Inputs || !FilterGraph)
	{
		check(false);
	}

	char args[100];
	snprintf(args, sizeof(args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		VideoEncoderCodecContext->width, VideoEncoderCodecContext->height, VideoEncoderCodecContext->pix_fmt,
		time_base.num, time_base.den,
		VideoEncoderCodecContext->sample_aspect_ratio.num, VideoEncoderCodecContext->sample_aspect_ratio.den);

	int Ret = avfilter_graph_create_filter(&BufferSrcContext, buffersrc, "in",
	                                       args, NULL, FilterGraph);
	if (Ret < 0)
	{
		check(false);
	}
	Ret = avfilter_graph_create_filter(&BufferSinkContext, buffersink, "out",
		NULL, NULL, FilterGraph);
	if (Ret < 0)
	{
		check(false);
	}


	

	

	Ret = av_opt_set_int_list(BufferSinkContext, "pix_fmts", pix_fmts,
		AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (Ret < 0)
	{
		check(false);
	}

	Outputs->name = av_strdup("in");
	Outputs->filter_ctx = BufferSrcContext;
	Outputs->pad_idx = 0;
	Outputs->next = NULL;

	Inputs->name = av_strdup("out");
	Inputs->filter_ctx = BufferSinkContext;
	Inputs->pad_idx = 0;
	Inputs->next = NULL;
	
	if ((Ret = avfilter_graph_parse_ptr(FilterGraph, TCHAR_TO_ANSI(*FilterDescription),
		&Inputs, &Outputs, NULL)) < 0)
	{
		check(false);
	}
	if ((Ret = avfilter_graph_config(FilterGraph, NULL)) < 0)
	{
		check(false);
	}
}

uint32 UCaptureSubsystemDirector::FormatSize_X(uint32 x)
{
	while ((x % 32) != 0)
	{
		++x;
	}
	return x;
}

void UCaptureSubsystemDirector::Encode_Finish()
{
	if (OutFormatContext)
	{
		av_write_trailer(OutFormatContext);
		avio_close(OutFormatContext->pb);
		avformat_free_context(OutFormatContext);
	}

	if (VideoEncoderCodecContext)
	{
		avcodec_free_context(&VideoEncoderCodecContext);
		avcodec_close(VideoEncoderCodecContext);
		av_free(VideoEncoderCodecContext);
	}

	if (AudioEncoderCodecContext)
	{
		avcodec_free_context(&AudioEncoderCodecContext);
		avcodec_close(AudioEncoderCodecContext);
		av_free(AudioEncoderCodecContext);
	}
	if (SWRContext)
	{
		swr_close(SWRContext);
		swr_free(&SWRContext);
		sws_freeContext(SwsContext);
	}

	avfilter_graph_free(&FilterGraph);
	avfilter_inout_free(&Inputs);
	avfilter_inout_free(&Outputs);

	av_frame_free(&VideoFrame);
	av_frame_free(&AudioFrame);
}

void UCaptureSubsystemDirector::LogErrorUE(int ErrorNum,bool bFatal)
{
	char * Code = nullptr;
	int Len=0;
	
	av_make_error_string(Code,Len,ErrorNum);
	const FString Error(Len,Code);
	if(bFatal)
	{
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("Error: %s"),*Error);
	}else
	{
		UE_LOG(LogCaptureSubsystem,Error,TEXT("Error: %s"),*Error);
	}
}
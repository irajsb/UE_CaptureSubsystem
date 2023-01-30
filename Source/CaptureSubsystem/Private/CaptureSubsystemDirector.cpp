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
#include "CaptureSubsystem.h"
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
		IsDestroy = true;
		if (FAudioDeviceHandle AudioDeviceHandle = GetWorld()->GetAudioDevice())
		{
		
	
			AudioDeviceHandle->UnregisterSubmixBufferListener(this);
		}
		Runnable->Stop();
		
		
		
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


	if (FAudioDeviceHandle AudioDeviceHandle = World->GetAudioDevice())
	{
		
	
		AudioDeviceHandle->RegisterSubmixBufferListener(this);
	}
}

void UCaptureSubsystemDirector::Initialize_Director(UWorld* World,FVideoCaptureOptions CaptureOptions)
{
	
	
	//avfilter_register()
	
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
		if (const int Err=avformat_alloc_output_context2(&OutFormatContext, NULL, "flv", TCHAR_TO_ANSI(*(Options.OutFileName)));Err < 0)
		{
			LogErrorUE(Err,true);
		}
	}
	else
	{
		if (const int Err=avformat_alloc_output_context2(&OutFormatContext, NULL, NULL, TCHAR_TO_ANSI(*(Options.OutFileName)));Err < 0)
		{
			UE_LOG(LogCaptureSubsystem,Error,TEXT("potential File name error , Check file extention %s"),*Options.OutFileName)
			LogErrorUE(Err,true);
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

bool UCaptureSubsystemDirector::Tick(float time)
{
	TickTime += time;


	if(IsDestroy)
	{
		if(Runnable->IsFinished())
		{
			Runnable->video_encode_delegate.Unbind();
			Runnable->GetAudioProcessDelegate().Unbind();
			Runnable->GetAudioTimeProcessDelegate().Unbind();

		
			RunnableThread->Kill(true);
			delete RunnableThread;
			RunnableThread=nullptr;
		
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
		}else
		{
			UE_LOG(LogTemp,Warning,TEXT("Waiting for encoding to finish"));
		}
	}
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
	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCaptureSubsystemDirector::Tick));
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
	const AVCodec* AudioCodec = avcodec_find_encoder_by_name(audioencoder_name);
	OutAudioStream = avformat_new_stream(OutFormatContext, AudioCodec);
	AudioIndex = OutAudioStream->index;
	AudioEncoderCodecContext = avcodec_alloc_context3(AudioCodec);

	if (!OutAudioStream)
	{
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("No out audio stream"));
	}
	AudioEncoderCodecContext->codec_id = AV_CODEC_ID_AAC;
	AudioEncoderCodecContext->bit_rate = 120000;
	AudioEncoderCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
	AudioEncoderCodecContext->sample_rate = 48000;
	AudioEncoderCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
	AudioEncoderCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
	
	
	if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		AudioEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	AudioEncoderCodecContext->codec_tag = 0;
	OutAudioStream->codecpar->codec_tag = 0;

	;
		if(const int ErrorResult=	avcodec_open2(AudioEncoderCodecContext, AudioCodec, NULL);ErrorResult<0)
		{
			LogErrorUE(ErrorResult,true);
		}
	
	avcodec_parameters_from_context(OutAudioStream->codecpar, AudioEncoderCodecContext);

	AudioFrame = av_frame_alloc();
	AudioFrame->nb_samples = AudioEncoderCodecContext->frame_size;
	AudioFrame->format = AudioEncoderCodecContext->sample_fmt;
	AudioFrame->channel_layout=AV_CH_LAYOUT_STEREO;
	if(const int Error= av_frame_get_buffer(AudioFrame,0);Error<0)
	{
		LogErrorUE(Error,true);	
	}
	
	
}

void UCaptureSubsystemDirector::Create_Video_Encoder(bool is_use_NGPU, const char* out_file_name, int bit_rate)
{
	const AVCodec *EncoderCodec=nullptr;
	
	if (is_use_NGPU)
	{	if(IsRHIDeviceNVIDIA())
		{
			EncoderCodec = avcodec_find_encoder_by_name("h264_nvenc");
			if(EncoderCodec)
			{
				UE_LOG(LogTemp,Log,TEXT("nvenc added"));
			}
		}else if (IsRHIDeviceAMD())
		{
			EncoderCodec = avcodec_find_encoder_by_name("h264_amf");
			if(EncoderCodec)
			{
				UE_LOG(LogTemp,Log,TEXT("AMF"))
			}
		}
		if(!EncoderCodec)
		{
			EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
		}
	}
	else
	{
		EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!EncoderCodec)
	{
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("No encoder found"));
	}
	OutVideoStream = avformat_new_stream(OutFormatContext, EncoderCodec);

	VideoIndex = OutVideoStream->index;

	VideoEncoderCodecContext = avcodec_alloc_context3(EncoderCodec);
	if (!VideoEncoderCodecContext)
	{
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("No VideoEncoderCodecContext!"))
	}
	VideoEncoderCodecContext->bit_rate = bit_rate;
	//video_encoder_codec_context->rc_min_rate = bit_rate;
	//video_encoder_codec_context->rc_max_rate = bit_rate;
	//video_encoder_codec_context->bit_rate_tolerance = bit_rate;
	//video_encoder_codec_context->rc_buffer_size = bit_rate;
	//video_encoder_codec_context->rc_initial_buffer_occupancy = bit_rate * 3 / 4;
	const auto CorrectedWidth=OutWidth-16+OutWidth%16;
	const auto CorrectedHeight=OutHeight-16+OutHeight%16;

	VideoEncoderCodecContext->width = OutWidth<OutHeight?CorrectedWidth:OutWidth;
	VideoEncoderCodecContext->height = OutWidth<OutHeight?CorrectedHeight:OutHeight;
	VideoEncoderCodecContext->max_b_frames = 2;
	VideoEncoderCodecContext->time_base.num = 1;
	
	VideoEncoderCodecContext->time_base.den = Options.FPS;
	VideoEncoderCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	VideoEncoderCodecContext->me_range = 16;
	VideoEncoderCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
	//VideoEncoderCodecContext->profile = FF_PROFILE_H264_BASELINE;
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
	if (EncoderCodec)
		av_opt_set(VideoEncoderCodecContext->priv_data, "preset", "fast",
		           0);

	if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		VideoEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	
	
	if ( const int Err=avcodec_open2(VideoEncoderCodecContext, EncoderCodec, NULL); Err< 0)
	{
		LogErrorUE(Err,true);
		
	}

	 
	if (const int Err= avcodec_parameters_from_context(OutVideoStream->codecpar, VideoEncoderCodecContext);Err< 0)
	{
		LogErrorUE(Err,true);
	}

	VideoFrame = av_frame_alloc();
	if (!VideoFrame)
	{
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("Video frame alloc failed"));
	}

	avio_open(&OutFormatContext->pb, out_file_name, AVIO_FLAG_WRITE);
	int ret = av_image_alloc(
		VideoFrame->data,
		VideoFrame->linesize,
		OutWidth,
		OutHeight,
		VideoEncoderCodecContext->pix_fmt,
		32);
	if (ret < 0)
	{
		LogErrorUE(ret,true);
	}

	const int Crop=Options.OptionalCaptureAspectRatio.IsZero()?GameTexture->GetSizeX(): (GameTexture->GetSizeY()*Options.OptionalCaptureAspectRatio.X)/Options.OptionalCaptureAspectRatio.Y;

	SwsContext = sws_getCachedContext(SwsContext,
	                                  Crop, GameTexture->GetSizeY(), AV_PIX_FMT_BGR24,
	                                  VideoEncoderCodecContext->width, VideoEncoderCodecContext->height, AV_PIX_FMT_YUV420P,
	                                  SWS_FAST_BILINEAR, 0, 0, 0);

	if (const int Err=avformat_write_header(OutFormatContext, NULL);Err < 0)
	{
		LogErrorUE(Err,true);
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
	AVPacket* AVPacket = av_packet_alloc();
	
	
	

	const int Count = swr_convert(SWRContext, OutputChannels, 4096, &data, 1024);
	
	
	if(Count<0)
	{
		LogErrorUE(Count,true);
	}
	if(const int Error= av_frame_make_writable(AudioFrame);Error<0)
	{
		LogErrorUE(Error,true);	
	}
	AudioFrame->data[0] = OutputChannels[0];
	AudioFrame->data[1] = OutputChannels[1];
	AudioFrame->nb_samples=Count;
	
	Set_Audio_Volume(AudioFrame);
	
	
	if (const auto ErrorNum=avcodec_send_frame(AudioEncoderCodecContext,AudioFrame);ErrorNum<0 )
	{
		LogErrorUE(ErrorNum,true);
		
	}
	
	
	while (avcodec_receive_packet(AudioEncoderCodecContext,AVPacket)==0)
	{
		AVPacket->pts = AVPacket->dts = av_rescale_q(
			(CurrentAudioTime + Options.AudioDelay) / av_q2d({ 1,48000 }),
			{ 1,48000 },
			OutAudioStream->time_base);
		
		AVPacket->duration = av_rescale_q(
			AVPacket->duration,
			{ 1,48000 },
			OutAudioStream->time_base);

		AVPacket->stream_index = AudioIndex;
		if(const int Err=av_write_frame(OutFormatContext, AVPacket);Err<0)
		{
			LogErrorUE(Err,false);
		}
		av_packet_unref(AVPacket);
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

	if (const int Err=av_buffersrc_add_frame_flags(BufferSrcContext, VideoFrame, AV_BUFFERSRC_FLAG_KEEP_REF);Err < 0)
	{
		LogErrorUE(Err,true);
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
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("Filter allocation error"));
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
		LogErrorUE(Ret,true);
	}
	Ret = avfilter_graph_create_filter(&BufferSinkContext, buffersink, "out",
	                                   NULL, NULL, FilterGraph);
	if (Ret < 0)
	{
		LogErrorUE(Ret,true);
	}


	

	

	Ret = av_opt_set_int_list(BufferSinkContext, "pix_fmts", pix_fmts,
	                          AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (Ret < 0)
	{
		LogErrorUE(Ret,true);
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
		LogErrorUE(Ret,true);
	}
	if ((Ret = avfilter_graph_config(FilterGraph, NULL)) < 0)
	{
		LogErrorUE(Ret,true);
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
	char Errbuf[255];
	
	av_strerror(ErrorNum,Errbuf,sizeof(Errbuf));
	const FString Error(Errbuf);
	
	if(bFatal)
	{
		UE_LOG(LogCaptureSubsystem,Fatal,TEXT("Error: %s"),*Error);
	}else
	{
		UE_LOG(LogCaptureSubsystem,Error,TEXT("Error: %s"),*Error);
	}
}

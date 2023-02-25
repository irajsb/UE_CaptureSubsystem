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
#include "Async/Async.h"
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

	void UCaptureSubsystemDirector::DestroyDirector()
	{
		if (!IsDestroy)
		{
			IsDestroy = true;
			if (FAudioDeviceHandle AudioDeviceHandle = GetWorld()->GetAudioDevice())
			{
				AudioDeviceHandle->UnregisterSubmixBufferListener(this);
			}
			if(Runnable)
			{
				Runnable->Stop();
			}
		}
	}

	void UCaptureSubsystemDirector::EndWindowReader(const bool i)
	{
		DestroyDirector();
		this->RemoveFromRoot();
		this->ConditionalBeginDestroy();
		this->BeginDestroy();
	}

	void UCaptureSubsystemDirector::EndWindowReader_StandardGame(void* i)
	{
		DestroyDirector();
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

	void UCaptureSubsystemDirector::Initialize_Director(UWorld* World, FVideoCaptureOptions CaptureOptions, UVideoCaptureSubsystem* InSubsystem)
	{
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating Director"));

		this->Subsystem=InSubsystem;
		avformat_network_init();
		
		Options = CaptureOptions;
		VideoTickTime = static_cast<float>(1) / static_cast<float>(Options.FPS);
		
		GameWindow = GEngine->GameViewport->GetWindow().Get();
		OutWidth = FormatSize_X(GEngine->GameViewport->Viewport->GetSizeXY().X);
		OutHeight = GEngine->GameViewport->Viewport->GetSizeXY().Y;

		const int Crop = Options.OptionalCaptureAspectRatio.IsZero()
			                 ? OutWidth
			                 : (OutHeight * Options.OptionalCaptureAspectRatio.X) / Options.OptionalCaptureAspectRatio.Y;
		OutWidth = Crop;


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
			OutWidth = FCString::Atoi(*Str_width);
			OutHeight = FCString::Atoi(*Str_height);
		}
		FilterDescription.Append("[in]");
		FilterDescription.Append("scale=");
		FilterDescription.Append(FString::FromInt(OutWidth));
		FilterDescription.Append(":");
		FilterDescription.Append(FString::FromInt(OutHeight));
		FilterDescription.Append("[out]");


		const int IsUseRTMP = Options.OutFileName.Find("rtmp");
		if (IsUseRTMP == 0)
		{
			if (const int Err = avformat_alloc_output_context2(&OutFormatContext, nullptr, "flv",
			                                                   TCHAR_TO_ANSI(*(Options.OutFileName))); Err < 0)
			{
				LogErrorUE("Format allocate error flv",Err, false);
			}
		}
		else
		{
			if (const int Err = avformat_alloc_output_context2(&OutFormatContext, nullptr, nullptr,
			                                                   TCHAR_TO_ANSI(*(Options.OutFileName))); Err < 0)
			{
				UE_LOG(LogCaptureSubsystem, Error, TEXT("potential File name error , Check file extention %s"),
				       *Options.OutFileName)
				LogErrorUE("Format allocate context error ",Err, false);
			}
		}
		
		Create_Audio_Encoder("aac");
		//bind delegate for get video data and audio data 
		Begin_Receive_VideoData();
		Begin_Receive_AudioData(World);
		//End PIE delegate and tick delegate
		AddEndFunction();
		AddTickFunction();
	}

	void UCaptureSubsystemDirector::Begin_Receive_VideoData()
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(
			this, &UCaptureSubsystemDirector::OnBackBufferReady_RenderThread);
	}

	void UCaptureSubsystemDirector::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
	{
		if (GameWindow == &SlateWindow)
		{
			if (TickTime >= VideoTickTime)
			{
				const UCaptureGameViewportClient* ViewportClient = static_cast<UCaptureGameViewportClient*>(GetWorld()->
					GetGameViewport());
				auto Texture = ViewportClient->MyRenderTarget->GetResource()->GetTexture2DRHI();

				GameTexture = Options.ShowUI ? BackBuffer : Texture;
				TickTime -= VideoTickTime;
				if (!Runnable)
				{
					CreateEncodeThread();
					Alloc_Video_Filter();
				}
				GetScreenVideoData();
			}
		}
	}

	bool UCaptureSubsystemDirector::Tick(float DeltaTime)
	{
		FrameDeltaTime=DeltaTime;
		if(FrameDeltaTime<1.0/Options.FPS)
		{
			FrameDeltaTime=1.0/Options.FPS;
		}
		TickTime += DeltaTime;
		if (IsDestroy)
		{
			
			if (Runnable&&Runnable->IsFinished())
			{
				Runnable->VideoEncodeDelegate.Unbind();
				Runnable->AudioEncodeDelegate.Unbind();
				


				RunnableThread->Kill(true);
				delete RunnableThread;
				RunnableThread = nullptr;

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
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Waiting for encoding to finish"));
			}
		}
		return true;
	}

	void UCaptureSubsystemDirector::AddEndFunction()
	{
		if (GameMode == EWorldType::Game)
		{
			FSlateApplication::Get().GetRenderer()->OnSlateWindowDestroyed().AddUObject(
				this, &UCaptureSubsystemDirector::EndWindowReader_StandardGame);
		}
	#if WITH_EDITOR
		if (GameMode == EWorldType::PIE)
		{
			FEditorDelegates::EndPIE.AddUObject(this, &UCaptureSubsystemDirector::EndWindowReader);
		}
	#endif
	}

	void UCaptureSubsystemDirector::AddTickFunction()
	{
		TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UCaptureSubsystemDirector::Tick));
	}

	void UCaptureSubsystemDirector::GetScreenVideoData()
	{
		FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

		SCOPED_DRAW_EVENT(RHICmdList, CaptureEvent);

		RHICmdList.TransitionResource(ERHIAccess::ReadOnlyMask, GameTexture);

		TextureData = static_cast<uint8*>(RHICmdList.LockTexture2D(GameTexture->GetTexture2D(), 0,
		                                                           EResourceLockMode::RLM_ReadOnly,
		                                                           TextureStride, false));
		if (Runnable&&!IsDestroy)
		{
			Runnable->InsertVideo(TextureData,FrameDeltaTime);
			
		}
		RHICmdList.UnlockTexture2D(GameTexture, 0, false);
	}

	void UCaptureSubsystemDirector::CreateEncodeThread()
	{
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating Encoder Thread"));
		Create_Video_Encoder(Options.UseGPU, TCHAR_TO_ANSI(*Options.OutFileName), Options.VideoBitRate);

		Runnable = new FEncoderThread();
		Runnable->CreateVideoQueue();
		BuffBgr = static_cast<uint8_t*>(FMemory::Realloc(BuffBgr, 3 * GameTexture->GetSizeX() * GameTexture->GetSizeY()));
		Runnable->VideoEncodeDelegate.BindUObject(this, &UCaptureSubsystemDirector::Encode_Video_Frame);

		RunnableThread = FRunnableThread::Create(Runnable, TEXT("EncoderThread"));
	}


	void UCaptureSubsystemDirector::Create_Audio_Encoder(const char* EncoderName)
	{
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating Audio Encoder "));
		
		const AVCodec* AudioCodec = avcodec_find_encoder_by_name(EncoderName);
		OutAudioStream = avformat_new_stream(OutFormatContext, AudioCodec);
		AudioIndex = OutAudioStream->index;
		AudioEncoderCodecContext = avcodec_alloc_context3(AudioCodec);

		if (!OutAudioStream)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("No out audio stream"));
		}
		AudioEncoderCodecContext->codec_id = AV_CODEC_ID_AAC;
		AudioEncoderCodecContext->bit_rate = 120000;
		AudioEncoderCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
		AudioEncoderCodecContext->sample_rate = 48000;
		AudioEncoderCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
		AudioEncoderCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;
		

		if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			AudioEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}

		AudioEncoderCodecContext->codec_tag = 0;
		OutAudioStream->codecpar->codec_tag = 0;

		if (const int ErrorResult = avcodec_open2(AudioEncoderCodecContext, AudioCodec, nullptr); ErrorResult < 0)
		{
			LogErrorUE("Audio codec open error",ErrorResult, false);
		}

		avcodec_parameters_from_context(OutAudioStream->codecpar, AudioEncoderCodecContext);

		AudioFrame = av_frame_alloc();
		AudioFrame->nb_samples = AudioEncoderCodecContext->frame_size;
		AudioFrame->format = AudioEncoderCodecContext->sample_fmt;
		AudioFrame->channel_layout = AV_CH_LAYOUT_STEREO;
		if (const int Error = av_frame_get_buffer(AudioFrame, 0); Error < 0)
		{
			LogErrorUE("Frame get buffer error",Error, false);
		}
	}

	void UCaptureSubsystemDirector::Create_Video_Encoder(bool UseGPU, const char* out_file_name, int bit_rate)
	{
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating Video encoder"));
		const AVCodec* EncoderCodec = nullptr;

		if (UseGPU)
		{
			if (IsRHIDeviceNVIDIA())
			{
				EncoderCodec = avcodec_find_encoder_by_name("h264_nvenc");
				if (EncoderCodec)
				{
					UE_LOG(LogTemp, Log, TEXT("nvenc added"));
				}
			}
			else if (IsRHIDeviceAMD())
			{
				EncoderCodec = avcodec_find_encoder_by_name("h264_amf");
				if (EncoderCodec)
				{
					UE_LOG(LogTemp, Log, TEXT("AMF"))
				}
			}
			if (!EncoderCodec)
			{
				EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
			}
		}
		else
		{
			EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
		}
		if (!EncoderCodec)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("No encoder found"));
		}
		OutVideoStream = avformat_new_stream(OutFormatContext, EncoderCodec);

		VideoIndex = OutVideoStream->index;

		
		SetupEncoderContext(EncoderCodec,bit_rate);

		if (EncoderCodec)
		{
			av_opt_set(VideoEncoderCodecContext->priv_data, "preset", "fast",
			           0);
			av_opt_set_int(VideoEncoderCodecContext->priv_data,"gpu",0,0);
		}

		if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			VideoEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}


		if ( int Err = avcodec_open2(VideoEncoderCodecContext, EncoderCodec, nullptr); Err < 0)
		{
			//if hardware codec failed to init use software codec
			if(strcmp(EncoderCodec->name,"h264_nvenc")==0||strcmp(EncoderCodec->name,"h264_amf")==0)
			{
				EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
				UE_LOG(LogCaptureSubsystem,Warning,TEXT("Using software encoder"))
				SetupEncoderContext(EncoderCodec,bit_rate);
				
				if(avcodec_open2(VideoEncoderCodecContext, EncoderCodec, nullptr); Err < 0)
				{
					LogErrorUE("Video codec open error",Err, false);
				}
					
			}else
			{
				LogErrorUE("Video codec open error",Err, false);
			}
		}


		if (const int Err = avcodec_parameters_from_context(OutVideoStream->codecpar, VideoEncoderCodecContext); Err < 0)
		{
			LogErrorUE("avcodec_parameters_from_context  error",Err, false);
		}

		VideoFrame = av_frame_alloc();
		if (!VideoFrame)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("Video frame alloc failed"));
		}

		avio_open(&OutFormatContext->pb, out_file_name, AVIO_FLAG_WRITE);
		const int Return = av_image_alloc(
			VideoFrame->data,
			VideoFrame->linesize,
			OutWidth,
			OutHeight,
			VideoEncoderCodecContext->pix_fmt,
			32);
		if (Return < 0)
		{
			LogErrorUE("avio open error ",Return, false);
		}

		const int Crop = Options.OptionalCaptureAspectRatio.IsZero()
			                 ? GameTexture->GetSizeX()
			                 : (GameTexture->GetSizeY() * Options.OptionalCaptureAspectRatio.X) / Options.
			                 OptionalCaptureAspectRatio.Y;

		SwsContext = sws_getCachedContext(SwsContext,
		                                  Crop, GameTexture->GetSizeY(), AV_PIX_FMT_BGR24,
		                                  VideoEncoderCodecContext->width, VideoEncoderCodecContext->height,
		                                  AV_PIX_FMT_YUV420P,
		                                  SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

		if (const int Err = avformat_write_header(OutFormatContext, nullptr); Err < 0)
		{
			LogErrorUE("avformat write header error",Err, false);
		}

		Video_Frame_Duration = OutVideoStream->time_base.den / Options.FPS;
		
	}

	void UCaptureSubsystemDirector::Video_Frame_YUV_From_BGR(const uint8_t* RGB, uint32 LineSize) const
	{
		const int LineSizeA[1] = {static_cast<int>(3 * LineSize)};
		sws_scale(SwsContext, &RGB, LineSizeA, 0,
		          GameTexture->GetSizeY(), VideoFrame->data, VideoFrame->linesize);

		VideoFrame->width = OutWidth;
		VideoFrame->height = OutHeight;
		VideoFrame->format = AV_PIX_FMT_YUV420P;
	}

	void UCaptureSubsystemDirector::Create_Audio_Swr(int NumChannels)
	{
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating SWR "));
		
		SWRContext = swr_alloc();

		av_opt_set_int(SWRContext, "in_channel_layout", NumChannels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_7POINT1, 0);
		av_opt_set_int(SWRContext, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
		av_opt_set_int(SWRContext, "in_sample_rate", 48000, 0);
		av_opt_set_int(SWRContext, "out_sample_rate", 48000, 0);
		av_opt_set_sample_fmt(SWRContext, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
		av_opt_set_sample_fmt(SWRContext, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
		swr_init(SWRContext);
	}

	void UCaptureSubsystemDirector::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples,
	                                                  int32 NumChannels, const int32 SampleRate, double AudioClock)
	{
		if (!Runnable||IsDestroy)
		{
			return;
		}
		if (!Runnable->IsAudioThreadInitialized())
		{
			Runnable->CreateAudioQueue();
			Runnable->AudioEncodeDelegate.BindUObject(this, &UCaptureSubsystemDirector::Encode_Audio_Frame);
			
		}
		if (!SWRContext)
		{
			Create_Audio_Swr(NumChannels);
		}

		if (InitialAudioTime < 0)
		{
			InitialAudioTime = AudioClock;
		}
		AudioClock = AudioClock - InitialAudioTime;
		Runnable->InsertAudio(AudioData,AudioClock);
	}

	void UCaptureSubsystemDirector::Encode_Audio_Frame(const FAudioData& AudioData)
	{
		UE_LOG(LogTemp,Log,TEXT("Encoding audio "));
		const uint8_t* Data =static_cast<uint8*>( AudioData.Data);
		CurrentAudioTime = AudioData.Time;
		AVPacket* AVPacket = av_packet_alloc();
		

		const int Count = swr_convert(SWRContext, OutputChannels, 4096, &Data, 1024);


		if (Count < 0)
		{
			LogErrorUE("swr convert error",Count, false);
		}
		if (const int Error = av_frame_make_writable(AudioFrame); Error < 0)
		{
			LogErrorUE("av_frame_make_writable error ",Error, false);
		}
		AudioFrame->data[0] = OutputChannels[0];
		AudioFrame->data[1] = OutputChannels[1];
		AudioFrame->nb_samples = Count;

		Set_Audio_Volume(AudioFrame);


		if (const auto ErrorNum = avcodec_send_frame(AudioEncoderCodecContext, AudioFrame); ErrorNum < 0)
		{
			LogErrorUE("avcodec_send_frame audio thread",ErrorNum, false);
		}


		while (avcodec_receive_packet(AudioEncoderCodecContext, AVPacket) == 0)
		{
			AVPacket->pts = AVPacket->dts = av_rescale_q(
				(CurrentAudioTime + Options.AudioDelay) / av_q2d({1, 48000}),
				{1, 48000},
				OutAudioStream->time_base);

			AVPacket->duration = av_rescale_q(
				AVPacket->duration,
				{1, 48000},
				OutAudioStream->time_base);

			AVPacket->stream_index = AudioIndex;
			if (const int Err = av_write_frame(OutFormatContext, AVPacket); Err < 0)
			{
				LogErrorUE("av_write_frame audio thread ",Err, false);
			}
			av_packet_unref(AVPacket);
		}
	}

	void UCaptureSubsystemDirector::Encode_Video_Frame(const FVideoData& VideoData)
	{
		uint8* TextureDataPtr =static_cast<uint8*> (VideoData.TextureData);
		uint8_t* FirstPointer = BuffBgr;
		AVPacket* VideoPacket = av_packet_alloc();
		
		const int Crop = Options.OptionalCaptureAspectRatio.IsZero()
			                 ? GameTexture->GetSizeX()
			                 : (GameTexture->GetSizeY() * Options.OptionalCaptureAspectRatio.X) / Options.
			                 OptionalCaptureAspectRatio.Y;
		const uint32 Difference = GameTexture->GetSizeX() - Crop;

		for (uint32 Row = 0; Row < GameTexture->GetSizeY(); ++Row)
		{
			uint32* PixelPtr = (uint32*)TextureDataPtr;
			for (uint32 Col = 0; Col < GameTexture->GetSizeX(); ++Col)
			{
				if (Col >= Difference / 2 && Col <= GameTexture->GetSizeX() - Difference / 2)
				{	if(PixelPtr)
				{
					const uint32 EncodedPixel = *PixelPtr;
					*(BuffBgr + 2) = (EncodedPixel >> 2) & 0xFF;
					*(BuffBgr + 1) = (EncodedPixel >> 12) & 0xFF;
					*(BuffBgr) = (EncodedPixel >> 22) & 0xFF;
					BuffBgr += 3;
				}
				}
				
				++PixelPtr;
			}
			

			TextureDataPtr += TextureStride;
		}
		BuffBgr = FirstPointer;

		
		//We need to pass actual calculated line size because of roundings of the int
		const int ShiftStride = Difference != 0 ? 1 : 0;
		Video_Frame_YUV_From_BGR(BuffBgr, ShiftStride + (GameTexture->GetSizeX() - Difference / 2) - Difference / 2);
		

		AVFrame* FilterFrame = av_frame_alloc();

		if (const int Err = av_buffersrc_add_frame_flags(BufferSrcContext, VideoFrame, AV_BUFFERSRC_FLAG_KEEP_REF); Err < 0)
		{
			LogErrorUE("av_buffersrc_add_frame_flags error ",Err, false);
		}
		while (true)
		{
			int ret = av_buffersink_get_frame(BufferSinkContext, FilterFrame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			{
				break;
			}
			if (ret < 0)
			{
				break;
			}
			if (ret >= 0)
			{
				avcodec_send_frame(VideoEncoderCodecContext, FilterFrame);
				while (ret >= 0)
				{
					ret = avcodec_receive_packet(VideoEncoderCodecContext, VideoPacket);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					{
						av_packet_unref(VideoPacket);
						break;
					}
					if (ret < 0)
					{
						av_packet_unref(VideoPacket);
						break;
					}
					VideoPacket->stream_index = VideoIndex;
					
					VideoPacket->pts = VideoPacket->dts = OutVideoStream->time_base.den*VideoClock;
					VideoPacket->stream_index = VideoIndex;
					VideoClock+=FrameDeltaTime;
					VideoPacket->duration = OutVideoStream->time_base.den*VideoData.FrameDeltaTime;
					av_write_frame(OutFormatContext, VideoPacket);
					
				}
				av_packet_unref(VideoPacket);
			}
		}
		av_frame_unref(FilterFrame);
	}

	void UCaptureSubsystemDirector::Encode_SetCurrentAudioTime(uint8_t* rgb)
	{
		CurrentAudioTime = *(double*)rgb;
	}

	void UCaptureSubsystemDirector::Set_Audio_Volume(AVFrame* frame) const
	{
		float* Ch_Left = reinterpret_cast<float*>(frame->data[0]);
		float* Ch_Right = reinterpret_cast<float*>(frame->data[1]);
		for (int i = 0; i < frame->nb_samples; i++)
		{
			Ch_Left[i] *= Options.SoundVolume;
			Ch_Right[i] *= Options.SoundVolume;
		}
	}


	void UCaptureSubsystemDirector::Alloc_Video_Filter()
	{
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Allocating Video Filter "));
		
		Outputs = avfilter_inout_alloc();
		Inputs = avfilter_inout_alloc();
		const AVFilter* BufferSrc = avfilter_get_by_name("buffer");
		const AVFilter* BufferSink = avfilter_get_by_name("buffersink");
		const enum AVPixelFormat PixelFormats[] = {VideoEncoderCodecContext->pix_fmt, AV_PIX_FMT_NONE};
		const AVRational TimeBase = {1, 1000000};

		FilterGraph = avfilter_graph_alloc();
		if (!Outputs || !Inputs || !FilterGraph)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("Filter allocation error"));
		}

		char Args[100];
		snprintf(Args, sizeof(Args),
		         "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		         VideoEncoderCodecContext->width, VideoEncoderCodecContext->height, VideoEncoderCodecContext->pix_fmt,
		         TimeBase.num, TimeBase.den,
		         VideoEncoderCodecContext->sample_aspect_ratio.num, VideoEncoderCodecContext->sample_aspect_ratio.den);

		int Ret = avfilter_graph_create_filter(&BufferSrcContext, BufferSrc, "in",
		                                       Args, nullptr, FilterGraph);
		if (Ret < 0)
		{
			LogErrorUE("avfilter_graph_create_filter in ",Ret, false);
		}
		Ret = avfilter_graph_create_filter(&BufferSinkContext, BufferSink, "out",
		                                   nullptr, nullptr, FilterGraph);
		if (Ret < 0)
		{
			LogErrorUE("avfilter_graph_create_filter out",Ret, false);
		}


		Ret = av_opt_set_int_list(BufferSinkContext, "pix_fmts", PixelFormats,
		                          AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
		if (Ret < 0)
		{
			LogErrorUE("av_opt_set_int_list error ",Ret, false);
		}

		Outputs->name = av_strdup("in");
		Outputs->filter_ctx = BufferSrcContext;
		Outputs->pad_idx = 0;
		Outputs->next = nullptr;

		Inputs->name = av_strdup("out");
		Inputs->filter_ctx = BufferSinkContext;
		Inputs->pad_idx = 0;
		Inputs->next = nullptr;

		if ((Ret = avfilter_graph_parse_ptr(FilterGraph, TCHAR_TO_ANSI(*FilterDescription),
		                                    &Inputs, &Outputs, nullptr)) < 0)
		{
			LogErrorUE("avfilter_graph_parse_ptr",Ret, false);
		}
		if ((Ret = avfilter_graph_config(FilterGraph, nullptr)) < 0)
		{
			LogErrorUE("avfilter_graph_config",Ret, false);
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
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Finishing Encoding "));
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

	void UCaptureSubsystemDirector::LogErrorUE(FString ErrorMessage,int ErrorNum, bool bFatal)
	{
		char ErrorBuffer[255];

		av_strerror(ErrorNum, ErrorBuffer, sizeof(ErrorBuffer));
		const FString Error(ErrorBuffer);

		if (bFatal)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("%s Error: %s"),*ErrorMessage, *Error);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread,[&]()
			{UE_LOG(LogCaptureSubsystem, Error, TEXT("%s Error: %s"),*ErrorMessage, *Error);
			Subsystem->OnError.Broadcast(ErrorMessage+" FFMPEG ERROR:"+Error);
			Subsystem->EndCapture();
			});
			
		}
	}

	void UCaptureSubsystemDirector::SetupEncoderContext(const AVCodec* Codec,int BitRate)
	{

		if(VideoEncoderCodecContext)
		{
			avcodec_free_context(&VideoEncoderCodecContext);
			
		}
		VideoEncoderCodecContext = avcodec_alloc_context3(Codec);
		if (!VideoEncoderCodecContext)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("No VideoEncoderCodecContext!"))
		}
		VideoEncoderCodecContext->bit_rate = BitRate;
		const auto CorrectedWidth = OutWidth - 16 + OutWidth % 16;
		const auto CorrectedHeight = OutHeight - 16 + OutHeight % 16;
		//some encoders need multiple of 16 res
		VideoEncoderCodecContext->width = OutWidth<OutHeight?CorrectedWidth:OutWidth;
		VideoEncoderCodecContext->height = OutWidth<OutHeight?CorrectedHeight:OutHeight;
		VideoEncoderCodecContext->max_b_frames = 2;
		VideoEncoderCodecContext->time_base.num = 1;

		VideoEncoderCodecContext->time_base.den = Options.FPS;
		VideoEncoderCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
		VideoEncoderCodecContext->me_range = 16;
		VideoEncoderCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;
		VideoEncoderCodecContext->frame_number = 1;
		VideoEncoderCodecContext->qcompress = 0.8;
		VideoEncoderCodecContext->max_qdiff = 4;
		VideoEncoderCodecContext->level = 30;
		VideoEncoderCodecContext->gop_size = 25;
		VideoEncoderCodecContext->qmin = 18;
		VideoEncoderCodecContext->qmax = 28;
		VideoEncoderCodecContext->me_range = 16;
		VideoEncoderCodecContext->framerate = {Options.FPS, 1};
		
	}
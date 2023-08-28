	// Copyright iraj mohtasham aurelion.net 2023
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
	// Log a message indicating that the director is being created
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Creating Director"));

	// Set the subsystem
	this->Subsystem = InSubsystem;

	// Initialize the network for FFmpeg
	avformat_network_init();

	// Set the capture options and calculate the tick time based on the FPS
	Options = CaptureOptions;
	VideoTickTime = static_cast<float>(1) / static_cast<float>(Options.FPS);

	// Get the game window and set the output width and height
	GameWindow = GEngine->GameViewport->GetWindow().Get();
	OutWidth = FormatSize_X(GEngine->GameViewport->Viewport->GetSizeXY().X);
	OutHeight = GEngine->GameViewport->Viewport->GetSizeXY().Y;

	// Calculate the crop width based on the optional capture aspect ratio
	const int Crop = Options.OptionalCaptureAspectRatio.IsZero()
		? OutWidth
		: (OutHeight * Options.OptionalCaptureAspectRatio.X) / Options.OptionalCaptureAspectRatio.Y;
	OutWidth = Crop;

	// Reallocate memory for output channels
	OutputChannels[0] = static_cast<uint8_t*>(FMemory::Realloc(OutputChannels[0], 4096));
	OutputChannels[1] = static_cast<uint8_t*>(FMemory::Realloc(OutputChannels[1], 4096));

	// Parse the video filter and set the output width and height if provided
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

	// Build the filter description for FFmpeg
	FilterDescription.Append("[in]");
	FilterDescription.Append("scale=");
	FilterDescription.Append(FString::FromInt(OutWidth));
	FilterDescription.Append(":");
	FilterDescription.Append(FString::FromInt(OutHeight));
	FilterDescription.Append("[out]");

	// Check if the output file name contains "rtmp"
	const int IsUseRTMP = Options.OutFileName.Find("rtmp");

	// Allocate the output format context based on the file name
	if (IsUseRTMP == 0)
	{
		if (const int Err = avformat_alloc_output_context2(&OutFormatContext, nullptr, "flv", TCHAR_TO_ANSI(*(Options.OutFileName))); Err < 0)
		{
			LogErrorUE("Format allocate error flv", Err, false);
		}
	}
	else
	{
		if (const int Err = avformat_alloc_output_context2(&OutFormatContext, nullptr, nullptr, TCHAR_TO_ANSI(*(Options.OutFileName))); Err < 0)
		{
			UE_LOG(LogCaptureSubsystem, Error, TEXT("potential File name error, Check file extension %s"), *Options.OutFileName);
			LogErrorUE("Format allocate context error ", Err, false);
		}
	}

	// Create the audio encoder
	Create_Audio_Encoder("aac");

	// Bind delegates for receiving video and audio data
	Begin_Receive_VideoData();
	Begin_Receive_AudioData(World);

	// Add end function and tick function delegates
	AddEndFunction();
	AddTickFunction();
}

void UCaptureSubsystemDirector::Begin_Receive_VideoData()
{
	// Bind the OnBackBufferReady_RenderThread function to the OnBackBufferReadyToPresent event
	FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &UCaptureSubsystemDirector::OnBackBufferReady_RenderThread);
}


void UCaptureSubsystemDirector::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
	{
		// Check if the Slate window matches the game window
		if (GameWindow == &SlateWindow)
		{
			// Check if the tick time has reached the desired video tick time
			if (TickTime >= VideoTickTime)
			{
				// Get the viewport client and the texture to capture
				const UCaptureGameViewportClient* ViewportClient = static_cast<UCaptureGameViewportClient*>(GetWorld()->GetGameViewport());
				auto Texture = ViewportClient->MyRenderTarget->GetResource()->GetTexture2DRHI();

				// Determine the texture to use based on the capture options
				GameTexture = Options.ShowUI ? BackBuffer : Texture;

				// Decrease the tick time by the video tick time
				TickTime -= VideoTickTime;

				// Check if the encoding thread and video filter are not already created
				if (!Runnable)
				{
					CreateEncodeThread();
					Alloc_Video_Filter();
				}

				// Get the video data from the screen
				GetScreenVideoData();
			}
		}
	}

bool UCaptureSubsystemDirector::Tick(float DeltaTime)
	{
		// Set the frame delta time
		FrameDeltaTime = DeltaTime;

		// Ensure the frame delta time is not smaller than the desired frame rate
		if (FrameDeltaTime < 1.0 / Options.FPS)
		{
			FrameDeltaTime = 1.0 / Options.FPS;
		}

		// Increase the tick time by the delta time
		TickTime += DeltaTime;

		// Check if the subsystem is marked for destruction
		if (IsDestroy)
		{
			// Check if the encoding thread has finished and there are no remaining frames
			if (Runnable && Runnable->IsFinished() && !CheckForRemainingFrames())
			{
				// Unbind the video and audio encoding delegates
				Runnable->VideoEncodeDelegate.Unbind();
				Runnable->AudioEncodeDelegate.Unbind();

				// Kill the encoding thread and clean up memory
				RunnableThread->Kill(true);
				delete RunnableThread;
				RunnableThread = nullptr;

				delete Runnable;
				Runnable = nullptr;

				// Remove the back buffer ready event delegate
				FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);

				// Remove the pre-PIE ended delegate (only in editor builds)
				#if WITH_EDITOR
				FEditorDelegates::PrePIEEnded.Remove(EndPIEDelegateHandle);
#endif

				// Remove the tick delegate
				FTSTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);

				// Finish encoding and free memory
				Encode_Finish();
				FMemory::Free(OutputChannels[0]);
				FMemory::Free(OutputChannels[1]);
				FMemory::Free(BuffBgr);
			}
			else
			{
				// Log a warning message if still waiting for encoding to finish
				UE_LOG(LogCaptureSubsystem, Warning, TEXT("Waiting for encoding to finish"));
			}
		}

		return true;
	}

	void UCaptureSubsystemDirector::AddEndFunction()
{
	// Check if the game mode is set to "Game"
	if (GameMode == EWorldType::Game)
	{
		// Bind the EndWindowReader_StandardGame function to the OnSlateWindowDestroyed event
		FSlateApplication::Get().GetRenderer()->OnSlateWindowDestroyed().AddUObject(this, &UCaptureSubsystemDirector::EndWindowReader_StandardGame);
	}

	// Check if the game mode is set to "PIE" (only in editor builds)
#if WITH_EDITOR
	if (GameMode == EWorldType::PIE)
	{
		// Bind the EndWindowReader function to the EndPIE event
		FEditorDelegates::EndPIE.AddUObject(this, &UCaptureSubsystemDirector::EndWindowReader);
	}
#endif
}

void UCaptureSubsystemDirector::AddTickFunction()
{
	// Add a ticker delegate that calls the Tick function
	TickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCaptureSubsystemDirector::Tick));
}

void UCaptureSubsystemDirector::GetScreenVideoData()
{
	// Get the immediate RHICmdList
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

	SCOPED_DRAW_EVENT(RHICmdList, CaptureEvent);

	// Transition the game texture to read-only access
	RHICmdList.TransitionResource(ERHIAccess::ReadOnlyMask, GameTexture);

	// Lock the game texture and get the texture data
	TextureData = static_cast<uint8*>(RHICmdList.LockTexture2D(GameTexture->GetTexture2D(), 0, EResourceLockMode::RLM_ReadOnly, TextureStride, false));

	if (Runnable && !IsDestroy)
	{
		// Insert the video frame data into the video queue of the encoding thread
		Runnable->InsertVideo(TextureData, FrameDeltaTime);
	}

	// Unlock the game texture
	RHICmdList.UnlockTexture2D(GameTexture, 0, false);
}

void UCaptureSubsystemDirector::CreateEncodeThread()
{
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Creating Encoder Thread"));

	// Create the video encoder
	Create_Video_Encoder(Options.UseGPU, TCHAR_TO_ANSI(*Options.OutFileName), Options.VideoBitRate);

	Runnable = new FEncoderThread();
	Runnable->CreateVideoQueue();

	// Reallocate the BuffBgr memory block to match the game texture size
	BuffBgr = static_cast<uint8_t*>(FMemory::Realloc(BuffBgr, 3 * GameTexture->GetSizeX() * GameTexture->GetSizeY()));

	// Bind the Encode_Video_Frame function to the VideoEncodeDelegate of the encoding thread
	Runnable->VideoEncodeDelegate.BindUObject(this, &UCaptureSubsystemDirector::Encode_Video_Frame);

	// Create the encoding thread
	RunnableThread = FRunnableThread::Create(Runnable, TEXT("EncoderThread"));
}


	void UCaptureSubsystemDirector::Create_Audio_Encoder(const char* EncoderName)
{
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Creating Audio Encoder "));

	// Find the audio codec by name
	const AVCodec* AudioCodec = avcodec_find_encoder_by_name(EncoderName);

	// Create a new audio stream in the output format context
	OutAudioStream = avformat_new_stream(OutFormatContext, AudioCodec);
	AudioIndex = OutAudioStream->index;
	AudioEncoderCodecContext = avcodec_alloc_context3(AudioCodec);

	// Check if the audio stream is valid
	if (!OutAudioStream)
	{
		UE_LOG(LogCaptureSubsystem, Fatal, TEXT("No out audio stream"));
	}

	// Set the properties of the audio encoder codec context
	AudioEncoderCodecContext->codec_id = AV_CODEC_ID_AAC;
	AudioEncoderCodecContext->bit_rate = 120000;
	AudioEncoderCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
	AudioEncoderCodecContext->sample_rate = 48000;
	AudioEncoderCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
	AudioEncoderCodecContext->channel_layout = AV_CH_LAYOUT_STEREO;

	// Set the global header flag if supported by the output format
	if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
	{
		AudioEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	AudioEncoderCodecContext->codec_tag = 0;
	OutAudioStream->codecpar->codec_tag = 0;

	// Open the audio codec
	if (const int ErrorResult = avcodec_open2(AudioEncoderCodecContext, AudioCodec, nullptr); ErrorResult < 0)
	{
		LogErrorUE("Audio codec open error", ErrorResult, false);
	}

	// Fill the audio stream parameters from the codec context
	avcodec_parameters_from_context(OutAudioStream->codecpar, AudioEncoderCodecContext);

	// Allocate an audio frame
	AudioFrame = av_frame_alloc();
	AudioFrame->nb_samples = AudioEncoderCodecContext->frame_size;
	AudioFrame->format = AudioEncoderCodecContext->sample_fmt;
	AudioFrame->channel_layout = AV_CH_LAYOUT_STEREO;

	// Allocate buffers for the audio frame
	if (const int Error = av_frame_get_buffer(AudioFrame, 0); Error < 0)
	{
		LogErrorUE("Frame get buffer error", Error, false);
	}
}

	void UCaptureSubsystemDirector::Create_Video_Encoder(bool UseGPU, const char* out_file_name, int bit_rate)
{
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Creating Video encoder"));

	const AVCodec* EncoderCodec = nullptr;

	if (UseGPU)
	{
		if (IsRHIDeviceNVIDIA())
		{
			EncoderCodec = avcodec_find_encoder_by_name("h264_nvenc");
			if (EncoderCodec)
			{
				UE_LOG(LogCaptureSubsystem, Log, TEXT("nvenc added"));
			}
		}
		else if (IsRHIDeviceAMD())
		{
			EncoderCodec = avcodec_find_encoder_by_name("h264_amf");
			if (EncoderCodec)
			{
				UE_LOG(LogCaptureSubsystem, Log, TEXT("AMF"));
			}
		}

		// If no GPU encoder codec found, fall back to software encoder
		if (!EncoderCodec)
		{
			EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
		}
	}
	else
	{
		// Use software encoder
		EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}

	// Check if an encoder codec is found
	if (!EncoderCodec)
	{
		UE_LOG(LogCaptureSubsystem, Fatal, TEXT("No encoder found"));
	}

	// Create a new video stream in the output format context
	OutVideoStream = avformat_new_stream(OutFormatContext, EncoderCodec);
	VideoIndex = OutVideoStream->index;

	// Set up the video encoder codec context
	SetupEncoderContext(EncoderCodec, bit_rate);

	// Set additional options for GPU encoding
	if (EncoderCodec)
	{
		av_opt_set(VideoEncoderCodecContext->priv_data, "preset", "fast", 0);
		av_opt_set_int(VideoEncoderCodecContext->priv_data, "gpu", 0, 0);
	}

	// Set the global header flag if supported by the output format
	if (OutFormatContext->oformat->flags & AVFMT_GLOBALHEADER)
	{
		VideoEncoderCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	// Open the video codec
	int Err = avcodec_open2(VideoEncoderCodecContext, EncoderCodec, nullptr);
	if (Err < 0)
	{
		// If the hardware codec fails to initialize, switch to the software codec
		if (strcmp(EncoderCodec->name, "h264_nvenc") == 0 || strcmp(EncoderCodec->name, "h264_amf") == 0)
		{
			EncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
			UE_LOG(LogCaptureSubsystem, Warning, TEXT("Using software encoder"));
			SetupEncoderContext(EncoderCodec, bit_rate);

			Err = avcodec_open2(VideoEncoderCodecContext, EncoderCodec, nullptr);
			if (Err < 0)
			{
				LogErrorUE("Video codec open error", Err, false);
			}
		}
		else
		{
			LogErrorUE("Video codec open error", Err, false);
		}
	}

	// Fill the video stream parameters from the codec context
	Err = avcodec_parameters_from_context(OutVideoStream->codecpar, VideoEncoderCodecContext);
	if (Err < 0)
	{
		LogErrorUE("avcodec_parameters_from_context error", Err, false);
	}

	// Allocate a video frame
	VideoFrame = av_frame_alloc();
	if (!VideoFrame)
	{
		UE_LOG(LogCaptureSubsystem, Fatal, TEXT("Video frame alloc failed"));
	}

	// Open the output file
	avio_open(&OutFormatContext->pb, out_file_name, AVIO_FLAG_WRITE);

	// Allocate buffers for the video frame
	const int Return = av_image_alloc(
		VideoFrame->data,
		VideoFrame->linesize,
		OutWidth,
		OutHeight,
		VideoEncoderCodecContext->pix_fmt,
		32
	);
	if (Return < 0)
	{
		LogErrorUE("avio open error", Return, false);
	}

	// Determine the crop size based on the optional capture aspect ratio
	const int Crop = Options.OptionalCaptureAspectRatio.IsZero() ?
	                 GameTexture->GetSizeX() :
	                 (GameTexture->GetSizeY() * Options.OptionalCaptureAspectRatio.X) /
	                 Options.OptionalCaptureAspectRatio.Y;

	// Initialize the software scaler context
	SwsContext = sws_getCachedContext(
		SwsContext,
		Crop, GameTexture->GetSizeY(), AV_PIX_FMT_BGR24,
		VideoEncoderCodecContext->width, VideoEncoderCodecContext->height, AV_PIX_FMT_YUV420P,
		SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
	);

	// Write the format header to the output file
	Err = avformat_write_header(OutFormatContext, nullptr);
	if (Err < 0)
	{
		LogErrorUE("avformat write header error", Err, false);
	}

	// Calculate the duration of each video frame
	Video_Frame_Duration = OutVideoStream->time_base.den / Options.FPS;
}

	void UCaptureSubsystemDirector::Video_Frame_YUV_From_BGR(const uint8_t* RGB, uint32 LineSize) const
{
	// Scale the RGB frame to YUV using the software scaler context
	const int LineSizeA[1] = { static_cast<int>(3 * LineSize) };
	sws_scale(SwsContext, &RGB, LineSizeA, 0, GameTexture->GetSizeY(), VideoFrame->data, VideoFrame->linesize);

	// Set the video frame properties
	VideoFrame->width = OutWidth;
	VideoFrame->height = OutHeight;
	VideoFrame->format = AV_PIX_FMT_YUV420P;
}

void UCaptureSubsystemDirector::Create_Audio_Swr(int NumChannels)
{
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Creating SWR "));

	// Allocate an audio resampler context
	SWRContext = swr_alloc();

	// Set the input and output channel layouts, sample rates, and sample formats
	av_opt_set_int(SWRContext, "in_channel_layout", NumChannels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_7POINT1, 0);
	av_opt_set_int(SWRContext, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
	av_opt_set_int(SWRContext, "in_sample_rate", 48000, 0);
	av_opt_set_int(SWRContext, "out_sample_rate", 48000, 0);
	av_opt_set_sample_fmt(SWRContext, "in_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
	av_opt_set_sample_fmt(SWRContext, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

	// Initialize the audio resampler context
	swr_init(SWRContext);
}

void UCaptureSubsystemDirector::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples,
                                                  int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	if (!Runnable || IsDestroy)
	{
		return;
	}

	// Create audio queue and bind the audio encoding delegate if not already initialized
	if (!Runnable->IsAudioThreadInitialized())
	{
		Runnable->CreateAudioQueue();
		Runnable->AudioEncodeDelegate.BindUObject(this, &UCaptureSubsystemDirector::Encode_Audio_Frame);
	}

	// Create audio resampler if not already created
	if (!SWRContext)
	{
		Create_Audio_Swr(NumChannels);
	}

	// Set the initial audio time if not set
	if (InitialAudioTime < 0)
	{
		InitialAudioTime = AudioClock;
	}

	// Calculate the relative audio clock time
	AudioClock = AudioClock - InitialAudioTime;

	// Insert the audio data into the audio queue
	Runnable->InsertAudio(AudioData, AudioClock);
}

	void UCaptureSubsystemDirector::Encode_Audio_Frame(const FAudioData& AudioData)
{
	// Retrieve audio data and current audio time from the provided struct
	const uint8_t* Data = static_cast<uint8*>(AudioData.Data);
	CurrentAudioTime = AudioData.Time;

	// Allocate an AVPacket for encoding the audio frame
	AVPacket* AVPacket = av_packet_alloc();

	// Convert the audio data using the audio resampler context
	const int Count = swr_convert(SWRContext, OutputChannels, 4096, &Data, 1024);

	if (Count < 0)
	{
		LogErrorUE("swr convert error", Count, false);
	}

	// Make the audio frame writable and set the audio frame properties
	if (const int Error = av_frame_make_writable(AudioFrame); Error < 0)
	{
		LogErrorUE("av_frame_make_writable error ", Error, false);
	}
	AudioFrame->data[0] = OutputChannels[0];
	AudioFrame->data[1] = OutputChannels[1];
	AudioFrame->nb_samples = Count;

	// Set the audio volume of the audio frame
	Set_Audio_Volume(AudioFrame);

	// Send the audio frame to the audio encoder context
	if (const auto ErrorNum = avcodec_send_frame(AudioEncoderCodecContext, AudioFrame); ErrorNum < 0)
	{
		LogErrorUE("avcodec_send_frame audio thread", ErrorNum, false);
	}

	// Receive encoded audio packets from the audio encoder context
	while (avcodec_receive_packet(AudioEncoderCodecContext, AVPacket) == 0)
	{
		// Set the packet's presentation and decoding timestamps
		AVPacket->pts = AVPacket->dts = av_rescale_q(
			(CurrentAudioTime + Options.AudioDelay) / av_q2d({ 1, 48000 }),
			{ 1, 48000 },
			OutAudioStream->time_base);

		// Rescale the packet's duration
		AVPacket->duration = av_rescale_q(
			AVPacket->duration,
			{ 1, 48000 },
			OutAudioStream->time_base);

		// Set the packet's stream index
		AVPacket->stream_index = AudioIndex;

		// Write the audio packet to the output format context
		if (const int Err = av_write_frame(OutFormatContext, AVPacket); Err < 0)
		{
			LogErrorUE("av_write_frame audio thread ", Err, false);
		}

		// Unreference and free the audio packet
		av_packet_unref(AVPacket);
	}
}

	void UCaptureSubsystemDirector::Encode_Video_Frame(const FVideoData& VideoData)
{
	// Update the game clock with the frame delta time
	GameClock += VideoData.FrameDeltaTime;

	// Drop this frame until we reach the video frame
	if (GameClock < VideoClock)
	{
		return;
	}

	uint8* TextureDataPtr = static_cast<uint8*>(VideoData.TextureData);
	uint8_t* FirstPointer = BuffBgr;
	AVPacket* VideoPacket = av_packet_alloc();

	// Calculate crop and difference values based on the capture aspect ratio
	const int Crop = Options.OptionalCaptureAspectRatio.IsZero()
		             ? GameTexture->GetSizeX()
		             : (GameTexture->GetSizeY() * Options.OptionalCaptureAspectRatio.X) / Options.OptionalCaptureAspectRatio.Y;
	const uint32 Difference = GameTexture->GetSizeX() - Crop;

	// Convert the RGB texture data to BGR format
	for (uint32 Row = 0; Row < GameTexture->GetSizeY(); ++Row)
	{
		uint32* PixelPtr = (uint32*)TextureDataPtr;
		for (uint32 Col = 0; Col < GameTexture->GetSizeX(); ++Col)
		{
			if (Col >= Difference / 2 && Col <= GameTexture->GetSizeX() - Difference / 2)
			{
				if (PixelPtr)
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

	// Calculate the line size to pass to the YUV conversion function
	const int ShiftStride = Difference != 0 ? 1 : 0;
	Video_Frame_YUV_From_BGR(BuffBgr, ShiftStride + (GameTexture->GetSizeX() - Difference / 2) - Difference / 2);

	AVFrame* FilterFrame = av_frame_alloc();

	// If the game FPS is less than the video FPS, add duplicate frames
	while (VideoClock < GameClock)
	{
		VideoFrame->pts = VideoFrame->pkt_dts = OutVideoStream->time_base.den * VideoClock;
		VideoFrame->duration = av_rescale_q(1, AVRational{ 1, Options.FPS }, OutVideoStream->time_base);
		VideoClock += 1.f / Options.FPS;

		if (const int Err = av_buffersrc_add_frame_flags(BufferSrcContext, VideoFrame, AV_BUFFERSRC_FLAG_KEEP_REF); Err < 0)
		{
			LogErrorUE("av_buffersrc_add_frame_flags error ", Err, false);
		}
	}

	// Process the video frames through the filter chain and encode them
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

				av_interleaved_write_frame(OutFormatContext, VideoPacket);
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
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Allocating Video Filter "));

	// Allocate AVFilterInOut structures for inputs and outputs
	Outputs = avfilter_inout_alloc();
	Inputs = avfilter_inout_alloc();

	// Get the buffer source and buffer sink filters
	const AVFilter* BufferSrc = avfilter_get_by_name("buffer");
	const AVFilter* BufferSink = avfilter_get_by_name("buffersink");

	// Define the pixel formats array and time base
	const enum AVPixelFormat PixelFormats[] = { VideoEncoderCodecContext->pix_fmt, AV_PIX_FMT_NONE };
	const AVRational TimeBase = { 1, 1000000 };

	// Allocate the filter graph
	FilterGraph = avfilter_graph_alloc();

	// Check if allocation was successful
	if (!Outputs || !Inputs || !FilterGraph)
	{
		UE_LOG(LogCaptureSubsystem, Fatal, TEXT("Filter allocation error"));
	}

	// Create the buffer source filter
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
		LogErrorUE("avfilter_graph_create_filter in ", Ret, false);
	}

	// Create the buffer sink filter
	Ret = avfilter_graph_create_filter(&BufferSinkContext, BufferSink, "out",
		nullptr, nullptr, FilterGraph);
	if (Ret < 0)
	{
		LogErrorUE("avfilter_graph_create_filter out", Ret, false);
	}

	// Set the pixel formats for the buffer sink
	Ret = av_opt_set_int_list(BufferSinkContext, "pix_fmts", PixelFormats,
		AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (Ret < 0)
	{
		LogErrorUE("av_opt_set_int_list error ", Ret, false);
	}

	// Initialize the Outputs structure
	Outputs->name = av_strdup("in");
	Outputs->filter_ctx = BufferSrcContext;
	Outputs->pad_idx = 0;
	Outputs->next = nullptr;

	// Initialize the Inputs structure
	Inputs->name = av_strdup("out");
	Inputs->filter_ctx = BufferSinkContext;
	Inputs->pad_idx = 0;
	Inputs->next = nullptr;

	// Parse the filter graph description
	if ((Ret = avfilter_graph_parse_ptr(FilterGraph, TCHAR_TO_ANSI(*FilterDescription),
		&Inputs, &Outputs, nullptr)) < 0)
	{
		LogErrorUE("avfilter_graph_parse_ptr", Ret, false);
	}

	// Configure the filter graph
	if ((Ret = avfilter_graph_config(FilterGraph, nullptr)) < 0)
	{
		LogErrorUE("avfilter_graph_config", Ret, false);
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

	bool UCaptureSubsystemDirector::CheckForRemainingFrames() const
	{

		avcodec_send_frame(VideoEncoderCodecContext, nullptr);
	
		
	
		AVPacket* VideoPacket = av_packet_alloc();

		int ret =0;
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
			av_interleaved_write_frame(OutFormatContext, VideoPacket);
					
		}
		av_packet_unref(VideoPacket);

		return  false;
	}

void UCaptureSubsystemDirector::SetupEncoderContext(const AVCodec* Codec, int BitRate)
	{
		// Free the existing VideoEncoderCodecContext if it exists
		if (VideoEncoderCodecContext)
		{
			avcodec_free_context(&VideoEncoderCodecContext);
		}

		// Allocate a new VideoEncoderCodecContext
		VideoEncoderCodecContext = avcodec_alloc_context3(Codec);
		if (!VideoEncoderCodecContext)
		{
			UE_LOG(LogCaptureSubsystem, Fatal, TEXT("No VideoEncoderCodecContext!"))
		}

		// Set the bit rate
		VideoEncoderCodecContext->bit_rate = BitRate;

		// Calculate the corrected width and height (multiples of 16)
		const auto CorrectedWidth = OutWidth - 16 + OutWidth % 16;
		const auto CorrectedHeight = OutHeight - 16 + OutHeight % 16;
		

		// Set the width and height of the video codec context based on the orientation of the video
		VideoEncoderCodecContext->width = OutWidth < OutHeight ? CorrectedWidth : OutWidth;
		VideoEncoderCodecContext->height = OutWidth < OutHeight ? CorrectedHeight : OutHeight;

		// Set the maximum number of consecutive B-frames
		VideoEncoderCodecContext->max_b_frames = 1;

		// Set the time base of the video codec context (numerator: 1, denominator: frames per second)
		VideoEncoderCodecContext->time_base.num = 1;
		VideoEncoderCodecContext->time_base.den = Options.FPS;

		// Set the pixel format of the video codec context to YUV420P
		VideoEncoderCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

		// Set the motion estimation range
		VideoEncoderCodecContext->me_range = 16;

		// Set the codec type to video
		VideoEncoderCodecContext->codec_type = AVMEDIA_TYPE_VIDEO;

		// Set the frame number
		VideoEncoderCodecContext->frame_number = 1;

		// Set the quantization parameter for compression
		VideoEncoderCodecContext->qcompress = 0.8;

		// Set the maximum quantization difference between frames
		VideoEncoderCodecContext->max_qdiff = 4;

		// Set the level of the video encoding (e.g., H.264 level)
		VideoEncoderCodecContext->level = 30;

		// Set the size of the group of pictures (GOP)
		VideoEncoderCodecContext->gop_size = 25;

		// Set the minimum and maximum quantization parameters
		VideoEncoderCodecContext->qmin = 18;
		VideoEncoderCodecContext->qmax = 28;

		// Set the motion estimation range
		VideoEncoderCodecContext->me_range = 16;

		// Set the frame rate of the video codec context (frames per second)
		VideoEncoderCodecContext->framerate = { Options.FPS, 1 };
	}

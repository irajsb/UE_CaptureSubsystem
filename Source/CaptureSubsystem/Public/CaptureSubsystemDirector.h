// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AudioMixerDevice.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#include "VideoCaptureSubsystem.h"
#include "Containers/Ticker.h"
struct FVideoCaptureOptions;

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/mem.h"
#include "libswscale/swscale.h"
#include "libavutil/file.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libavutil/error.h"
#include "libswresample/swresample.h"
}
#include "CaptureSubsystemDirector.generated.h"

/**
 * 
 */
class FEncoderThread;


UCLASS(BlueprintType)
class CAPTURESUBSYSTEM_API UCaptureSubsystemDirector:public UObject,public ISubmixBufferListener
{
	GENERATED_BODY()
public:

	UCaptureSubsystemDirector();
	virtual ~UCaptureSubsystemDirector();
	UFUNCTION(BlueprintCallable)
	void Initialize_Director(UWorld* World,FVideoCaptureOptions CaptureOptions);
	void Begin_Receive_AudioData(UWorld* world);
	void Begin_Receive_VideoData();

	void Encode_Video_Frame(uint8_t *rgb);
	void Encode_SetCurrentAudioTime(uint8_t* rgb);
	void Encode_Audio_Frame(uint8_t *RawData);
	void Encode_Finish();
	
	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
	void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
	bool Tick(float time);

	void EndWindowReader(const bool i);
	void EndWindowReader_StandardGame(void* i);
	
	void DestroyDirector();
	
private:
	void Create_Video_Encoder(bool UseGPU, const char* out_file_name,int bit_rate);
	void Create_Audio_Encoder(const char* EncoderName);
	void Video_Frame_YUV_From_BGR(const uint8_t *RGB,uint32 LineSize) const;
	void Create_Audio_Swr(int NumChannels);
	void GetScreenVideoData();

	void AddTickFunction();
	void AddEndFunction();
	void CreateEncodeThread();
	void Set_Audio_Volume(AVFrame *frame) const;
	
	void Alloc_Video_Filter();
	static uint32 FormatSize_X(uint32 x);

	static void LogErrorUE(int ErrorNum, bool bFatal);

private:
	bool IsDestroy = false;
	FString FilterDescription;

	FVideoCaptureOptions Options;
	uint32 Video_Frame_Duration;
	float VideoTickTime;
	double CurrentAudioTime = 0.0;
	double InitialAudioTime = -1.0;
	

	uint32 OutWidth;
	uint32 OutHeight;

	FTexture2DRHIRef GameTexture;

	
	AVFilterInOut *Outputs;
	AVFilterInOut *Inputs;
	AVFilterGraph *FilterGraph;
	AVFilterContext *BufferSinkContext;
	AVFilterContext *BufferSrcContext;
	

	
	SWindow* GameWindow;
	TArray<FColor> TexturePixel;
	float TickTime = 0.0f;
	int64_t Video_Pts = 0;
	uint8_t* BuffBgr;
	int32_t VideoIndex;
	int32_t AudioIndex;
	uint8* TextureData=nullptr;
	uint32 TextureStride;
	FEncoderThread * Runnable;
	FRunnableThread* RunnableThread;

	AVFormatContext* OutFormatContext;
	AVCodecContext* VideoEncoderCodecContext;
	AVCodecContext* AudioEncoderCodecContext;

	SwsContext* SwsContext;
	AVStream* OutVideoStream;
	AVStream* OutAudioStream;
	SwrContext* SWRContext;
	uint8_t* OutputChannels[2];

	FTSTicker::FDelegateHandle TickDelegateHandle;
	FDelegateHandle EndPIEDelegateHandle;

	AVFrame* AudioFrame;
	AVFrame* VideoFrame;

	
	TEnumAsByte<EWorldType::Type> GameMode;
};



// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/CircularQueue.h"
#include "Containers/RingBuffer.h"

/**
 * 
 */

struct FVideoData
{
	void* TextureData;
	float FrameDeltaTime;
	FVideoData(float InDeltaTime,void* Data)
	{
		TextureData=Data;
		FrameDeltaTime=InDeltaTime;
	}
	FVideoData()
	{
		TextureData=nullptr;
		FrameDeltaTime=0.f;
	}
};

struct FAudioData
{
	void* Data;
	float Time;
	FAudioData(float InTime,void* InData)
	{
		Time=InTime;
		Data=InData;
	}
	FAudioData()
	{
		Data=nullptr;
		Time=0.f;
	};
};

class UCircleQueue;


DECLARE_DELEGATE_OneParam(FVideoEncodeDelegate, const FVideoData& )
DECLARE_DELEGATE_OneParam(FAudioEncodeDelegate, const FAudioData& )

class CAPTURESUBSYSTEM_API FEncoderThread :public FRunnable 
{
public:

	FEncoderThread();
	~FEncoderThread();
	virtual bool Init() override; 
	virtual uint32 Run() override; 
	virtual void Stop() override;  
	virtual void Exit() override; 
	
	void CreateVideoQueue();
	void CreateAudioQueue();
	bool IsAudioThreadInitialized() const;


	void InsertVideo(void* TextureData, float DeltaTime);
	bool InsertAudio(void* Data, float AudioClock) const;

	FCriticalSection VideoBufferMutex;
	FCriticalSection AudioMutex;

	bool bStopped = false;
	bool bExit = false;
	FVideoEncodeDelegate VideoEncodeDelegate;
	FAudioEncodeDelegate AudioEncodeDelegate;
	
	
	bool IsFinished() const;
private:
	void RunEncode();
	void EncodeVideo() const;
	void EncodeAudio() const;

private:

private:
	TUniquePtr< TCircularQueue<FAudioData> >AudioDataQueue;
	TUniquePtr< TCircularQueue<FVideoData> >VideoDataQueue;
	

};

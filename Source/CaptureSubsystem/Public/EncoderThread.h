// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "EncodeData.h"
#include "Containers/CircularQueue.h"

/**
 * 
 */


struct FAudioData
{
	uint8* Data;
	float Time;
	FAudioData(float InTime,uint8* InData)
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

DECLARE_DELEGATE_OneParam(VideoEncodeDelegate, uint8*)

class CAPTURESUBSYSTEM_API FEncoderThread :public FRunnable 
{
public:

	FEncoderThread();
	~FEncoderThread();
	virtual bool Init() override; 
	virtual uint32 Run() override; 
	virtual void Stop() override;  
	virtual void Exit() override; 
	
	void CreateQueue(int video_data_size ,int video_data_num);
	void CreateAudioQueue( int AudioDataSize, int AudioDataNum);
	bool IsAudioThreadInitialized() const;

	EncodeDelegate& GetAudioProcessDelegate() const;
	EncodeDelegate& GetAudioTimeProcessDelegate() const;

	bool InsertVideo(const uint8* Src);
	bool InsertAudio(uint8* Src, uint8* time);

	FCriticalSection VideoBufferMutex;
	FCriticalSection AudioMutex;

	bool bStopped = false;
	bool bExit = false;
	VideoEncodeDelegate video_encode_delegate;
	void GetBufferData(uint8* data);
	
	bool IsFinished() const;
private:
	void RunEncode();
	void EncodeVideo();
	void EncodeAudio() ;

private:
	
	UCircleQueue* VideoBufferQueue;
	UCircleQueue* AudioQueue;
	UCircleQueue* AudioTimeQueue;
	
	TUniquePtr< TCircularQueue<FAudioData> >AudioDataQueue;
	uint8* VideoData=nullptr;
};

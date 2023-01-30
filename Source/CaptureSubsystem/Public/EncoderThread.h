// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "EncodeData.h"

/**
 * 
 */
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
	void CreateAudioQueue( int auido_data_size, int auido_data_num);
	bool IsAudioThreadInitialized() const;

	EncodeDelegate& GetAudioProcessDelegate();
	EncodeDelegate& GetAudioTimeProcessDelegate();

	bool InsertVideo(uint8* Src);
	bool InsertAudio(uint8* Src,uint8* time);

	FCriticalSection VideoBufferMutex;
	FCriticalSection AudioMutex;

	bool bStopped = false;
	bool bExit = false;
	VideoEncodeDelegate video_encode_delegate;
	void GetBufferData(uint8* data);
	
	bool IsFinished();
private:
	void RunEncode();
	void EncodeVideo();
	void EncodeAudio();

private:

	UCircleQueue* VideoBufferQueue;
	UCircleQueue* AudioQueue;
	UCircleQueue* AudioTimeQueue;

	uint8* VideoData=nullptr;
};

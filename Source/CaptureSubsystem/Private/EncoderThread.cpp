// Fill out your copyright notice in the Description page of Project Settings.


#include "EncoderThread.h"

#include "CaptureSubsystem.h"


FEncoderThread::FEncoderThread()
{

	VideoDataQueue=nullptr;
	AudioDataQueue=nullptr;
}

FEncoderThread::~FEncoderThread()
{
	VideoDataQueue.Reset();
	AudioDataQueue.Reset();
}

bool FEncoderThread::Init()
{
	return true;
}

uint32 FEncoderThread::Run()
{
	while ((!bStopped) || (!IsFinished()))
	{
		RunEncode();
	}
	return 1;
}

void FEncoderThread::Stop()
{
	bStopped = true;
}

void FEncoderThread::Exit()
{
}

void FEncoderThread::CreateVideoQueue()
{
	UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating Video Queue"))
	VideoDataQueue=MakeUnique<TCircularQueue<FVideoData>>(60 );
}

void FEncoderThread::CreateAudioQueue()
{
	UE_LOG(LogCaptureSubsystem,Log,TEXT("Creating Audio Queue"))
	AudioDataQueue=MakeUnique<TCircularQueue<FAudioData>>(60 );
	
}

bool FEncoderThread::IsAudioThreadInitialized() const
{
	if (AudioDataQueue)
	{
		return true;
	}
	return false;
}


void FEncoderThread::InsertVideo(void* TextureData, float DeltaTime)
{
	if (bStopped)
	{
		return ;
	}
	if (!VideoDataQueue)
	{
		return ;
	}

	
		FScopeLock ScopeLock(&VideoBufferMutex);
		VideoDataQueue->Enqueue(FVideoData(DeltaTime,TextureData));
	

}

bool FEncoderThread::InsertAudio(void* Data, float AudioClock) const
{
	if (bStopped)
	{
		return false;
	}
	if (!AudioDataQueue)
	{
		return false;
	}
	AudioDataQueue->Enqueue(FAudioData(AudioClock,Data));

	return true;
}



void FEncoderThread::RunEncode()
{
	{
		FScopeLock ScopeLock(&AudioMutex);
		EncodeAudio();
	}

	{
		FScopeLock ScopeLock1(&VideoBufferMutex);
		{
			EncodeVideo();
		}
	}
}

void FEncoderThread::EncodeVideo() const
{
	FVideoData Data;
	
	if(VideoDataQueue->Dequeue(Data))
	{
		VideoEncodeDelegate.ExecuteIfBound(Data);
	}

}

void FEncoderThread::EncodeAudio() const
{

	FAudioData Data;
	if(!AudioDataQueue)
	{
		return;
	}
	if(AudioDataQueue->Dequeue(Data))
	{
		AudioEncodeDelegate.ExecuteIfBound(Data);
	}
	
	
}

bool FEncoderThread::IsFinished() const
{
	if(!VideoDataQueue||!AudioDataQueue)
	{
		return false;
	}
	return VideoDataQueue->IsEmpty()&&AudioDataQueue->IsEmpty();

}

// Fill out your copyright notice in the Description page of Project Settings.


#include "EncoderThread.h"


FEncoderThread::FEncoderThread()
{
	VideoBufferQueue = nullptr;
	AudioQueue = nullptr;
	AudioTimeQueue = nullptr;
	
}

FEncoderThread::~FEncoderThread()
{
	if (AudioQueue)
	{
		AudioQueue->RemoveFromRoot();
		AudioQueue->ConditionalBeginDestroy();
		AudioQueue->BeginDestroy();
	}
	if (VideoBufferQueue)
	{
		VideoBufferQueue->RemoveFromRoot();
		VideoBufferQueue->ConditionalBeginDestroy();
		VideoBufferQueue->BeginDestroy();
	}
	if (AudioTimeQueue)
	{
		AudioTimeQueue->RemoveFromRoot();
		AudioTimeQueue->ConditionalBeginDestroy();
		AudioTimeQueue->BeginDestroy();
	}
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

void FEncoderThread::CreateQueue(int video_data_size, int video_data_num)
{
	VideoBufferQueue = NewObject<UCircleQueue>();
	VideoBufferQueue->AddToRoot();
	VideoBufferQueue->Init(video_data_num, video_data_size);
	VideoBufferQueue->EncodeDelegate.BindRaw(this, &FEncoderThread::GetBufferData);
}

void FEncoderThread::CreateAudioQueue(const int AudioDataSize, const int AudioDataNum)
{
	AudioQueue = NewObject<UCircleQueue>();
	AudioQueue->AddToRoot();
	AudioQueue->Init(AudioDataNum, AudioDataSize);


	AudioTimeQueue = NewObject<UCircleQueue>();
	AudioTimeQueue->AddToRoot();
	AudioTimeQueue->Init(AudioDataNum, AudioDataNum * sizeof(double));
}

bool FEncoderThread::IsAudioThreadInitialized() const
{
	if (AudioQueue)
	{
		return true;
	}
	return false;
}

EncodeDelegate& FEncoderThread::GetAudioProcessDelegate() const
{
	return AudioQueue->EncodeDelegate;
}

EncodeDelegate& FEncoderThread::GetAudioTimeProcessDelegate() const
{
	return AudioTimeQueue->EncodeDelegate;
}

bool FEncoderThread::InsertVideo(const uint8* Src)
{
	if (bStopped)
	{
		return false;
	}
	if (!VideoBufferQueue)
	{
		return false;
	}
	{
		FScopeLock ScopeLock(&VideoBufferMutex);
		while (!VideoBufferQueue->InsertEncodeData(Src))
		{
			VideoBufferQueue->ProcessEncodeData();
			EncodeVideo();
		}
	}
	return true;
}

bool FEncoderThread::InsertAudio(uint8* Src, uint8* time)
{
	if (bStopped)
	{
		return false;
	}
	if (!AudioQueue || !AudioTimeQueue)
	{
		return false;
	}

	FScopeLock ScopeLock(&AudioMutex);
	while (!AudioQueue->InsertEncodeData(Src) || !AudioTimeQueue->InsertEncodeData(time))
	{
		EncodeAudio();
	}

	return true;
}

void FEncoderThread::GetBufferData(uint8* data)
{
	VideoData = data;
}

void FEncoderThread::RunEncode()
{
	{
		FScopeLock ScopeLock(&AudioMutex);
		EncodeAudio();
	}

	{
		FScopeLock ScopeLock1(&VideoBufferMutex);

		if (VideoBufferQueue->ProcessEncodeData())
		{
			EncodeVideo();
		}
	}
}

void FEncoderThread::EncodeVideo()
{
	if (VideoData)
	{
		video_encode_delegate.ExecuteIfBound(VideoData);
		VideoData = nullptr;
	}
}

void FEncoderThread::EncodeAudio() 
{

	if (AudioQueue && AudioTimeQueue)
	{
		AudioTimeQueue->ProcessEncodeData();
		AudioQueue->ProcessEncodeData();
	}
	
}

bool FEncoderThread::IsFinished() const
{
	if (!AudioQueue || !AudioTimeQueue)
	{
		return false;
	}
	return AudioQueue->IsEmpty() && AudioTimeQueue->IsEmpty() && (!VideoData);
}

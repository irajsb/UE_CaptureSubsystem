// Fill out your copyright notice in the Description page of Project Settings.


#include "EncoderThread.h"


FEncoderThread::FEncoderThread()
{
	VideoBufferQueue=nullptr;
	AudioQueue=nullptr;
	AudioTimeQueue=nullptr;
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
	while (!IsDone)
	{
		RunEncode();
	}
	return 1;
}

void FEncoderThread::Stop()
{
	IsDone = true;
}

void FEncoderThread::Exit()
{
	
}

void FEncoderThread::CreateQueue(int video_data_size ,int video_data_num)
{
	VideoBufferQueue = NewObject<UCircleQueue>();
	VideoBufferQueue->AddToRoot();
	VideoBufferQueue->Init(video_data_num, video_data_size);
	VideoBufferQueue->encode_delegate.BindRaw(this, &FEncoderThread::GetBufferData);


}

void FEncoderThread::CreateAudioQueue(int auido_data_size, int auido_data_num)
{
	AudioQueue = NewObject<UCircleQueue>();
	AudioQueue->AddToRoot();
	AudioQueue->Init(auido_data_num, auido_data_size);


	AudioTimeQueue = NewObject<UCircleQueue>();
	AudioTimeQueue->AddToRoot();
	AudioTimeQueue->Init(auido_data_num, auido_data_num*sizeof(double));
}

bool FEncoderThread::IsAudioThreadInitialized() const
{
	if(AudioQueue)
		return true;
	return false;
}

EncodeDelegate& FEncoderThread::GetAudioProcessDelegate()
{
	return AudioQueue->encode_delegate; 
}

EncodeDelegate& FEncoderThread::GetAudioTimeProcessDelegate()
{
	return AudioTimeQueue->encode_delegate;
}

bool FEncoderThread::InsertVideo(uint8* Src)
{
	if (!VideoBufferQueue)
		return false;
	{
		FScopeLock ScopeLock(&VideoBufferMutex);		
		while (!VideoBufferQueue->InsertEncodeData(Src))
		{
			//GEngine->AddOnScreenDebugMessage(-1, 0.02f, FColor::Red, FString("video  now  encode"));
			VideoBufferQueue->PrcessEncodeData();
			EncodeVideo();
		}
	}		
	return true;
}

bool FEncoderThread::InsertAudio(uint8* Src, uint8* time)
{
	if (!AudioQueue||!AudioTimeQueue)
		return false;
	
		FScopeLock ScopeLock(&AudioMutex);

		while (!AudioQueue->InsertEncodeData(Src) || !AudioTimeQueue->InsertEncodeData(time))
		{
			//GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Red, FString("audio  now  encode"));
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
	bool IsNeedEncode = false;

	{
		FScopeLock ScopeLock(&AudioMutex);
		EncodeAudio();
	}

	{
		FScopeLock ScopeLock1(&VideoBufferMutex);
		IsNeedEncode = VideoBufferQueue->PrcessEncodeData();

		if (IsNeedEncode)
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
	if (AudioQueue&&AudioTimeQueue)
	{
		
		AudioTimeQueue->PrcessEncodeData();
		AudioQueue->PrcessEncodeData();
	}
}

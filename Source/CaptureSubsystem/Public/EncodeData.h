// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EncodeData.generated.h"
/**
 * 
 */

DECLARE_DELEGATE_OneParam(EncodeDelegate,uint8*)


class CAPTURESUBSYSTEM_API FEncodeData
{
public:

	FEncodeData();
	~FEncodeData();
	void InitializeData(int size);
	void SetEncodeData(const uint8* Src) const;
	uint8* GetData() const;
private:
	uint8* DataMemory;
	int datasize;
};

UCLASS()
class CAPTURESUBSYSTEM_API UCircleQueue:public UObject
{
	GENERATED_BODY()
public:
	UCircleQueue();
	~UCircleQueue();

	void Init(int queue_len,int data_size);
	bool InsertEncodeData(const uint8* Src);
	bool ProcessEncodeData();
	bool IsFull() const;
	bool IsEmpty() const;
	EncodeDelegate EncodeDelegate;
private:
	int QueueNum;
	//¶ÓÁÐ¿ÕÏÐÊýÁ¿
	int QueueFreeNum;
	int QueueHead;
	int QueueTail;
	FEncodeData* QueuePtr;
};




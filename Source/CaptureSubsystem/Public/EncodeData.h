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
	void SetEncodeData(uint8* Src);
	uint8* GetData();
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
	bool InsertEncodeData(uint8* Src);
	bool PrcessEncodeData();
	bool IsFull();
	bool IsEmpty();
	EncodeDelegate encode_delegate;
private:
	int queue_num;
	//¶ÓÁÐ¿ÕÏÐÊýÁ¿
	int queue_freenum;
	int queue_head;
	int queue_tail;
	FEncodeData* queue_ptr;
};




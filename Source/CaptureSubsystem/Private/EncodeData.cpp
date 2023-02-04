// Fill out your copyright notice in the Description page of Project Settings.


#include "EncodeData.h"

FEncodeData::FEncodeData():
	DataMemory(nullptr), datasize(0)
{
}

FEncodeData::~FEncodeData()
{
	if (DataMemory)
		FMemory::Free(DataMemory);
}

void FEncodeData::InitializeData(int size)
{
	DataMemory = static_cast<uint8*>(FMemory::Realloc(DataMemory, size));
	datasize = size;
}

void FEncodeData::SetEncodeData(const uint8* Src) const
{	
	FMemory::StreamingMemcpy(DataMemory, Src, datasize);	
}

uint8* FEncodeData::GetData() const
{
	return DataMemory != nullptr ? DataMemory : nullptr;
}

//////////////////////////////////////////////////////////////////

UCircleQueue::UCircleQueue():
	QueuePtr(nullptr)
{
	QueueHead = 0;
	QueueTail = 0;
}

UCircleQueue::~UCircleQueue()
{
	delete[] QueuePtr;
}

void UCircleQueue::Init(int queue_len, int data_sized)
{
	QueueNum = queue_len;
	QueueFreeNum = QueueNum;
	QueuePtr = new FEncodeData[QueueNum];
	for (int i = 0; i < QueueNum; ++i)
	{
		QueuePtr[i].InitializeData(data_sized);
	}
}

bool UCircleQueue::InsertEncodeData(const uint8* Src)
{
	if (IsFull())
		return false;
	QueuePtr[QueueTail].SetEncodeData(Src);
	--QueueFreeNum;
	QueueTail = (QueueTail + 1) % QueueNum;
	return true;
}

bool UCircleQueue::ProcessEncodeData()
{
	if (IsEmpty())
		return false;
	EncodeDelegate.ExecuteIfBound(QueuePtr[QueueHead].GetData());
	QueueHead= (QueueHead + 1) % QueueNum;
	++QueueFreeNum;
	return true;
}

bool UCircleQueue::IsFull() const
{
	return QueueFreeNum == 0 ? true : false;
}

bool UCircleQueue::IsEmpty() const
{
	return QueueFreeNum == QueueNum ? true : false;
}

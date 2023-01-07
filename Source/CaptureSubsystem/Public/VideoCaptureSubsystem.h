// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "VideoCaptureSubsystem.generated.h"

class UCaptureSubsystemDirector;
/**
 * 
 */
USTRUCT(BlueprintType)
struct FVideoCaptureOptions
{
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	FString OutFileName;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	FString VideoFilter;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	bool UseGPU;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	int FPS;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	int VideoBitRate;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	float AudioDelay;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	float SoundVolume;
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	bool ShowUI;
	//Will do (height*x)/y to get aspect ratio (useful horizontal Cropping like instagram square shaped videos). set zero to use default aspect ratio 
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	FVector2D OptionalCaptureAspectRatio;
	GENERATED_BODY()
	
};
UCLASS()
class CAPTURESUBSYSTEM_API UVideoCaptureSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()


	
	UFUNCTION(BlueprintCallable)
	void StartCapture(FVideoCaptureOptions Options);
	UFUNCTION(BlueprintCallable)
	void EndCapture(FVideoCaptureOptions Options);


	UPROPERTY(Transient)
	UCaptureSubsystemDirector* Director=nullptr;



	UFUNCTION(BlueprintCallable)
	void TakeScreenshot(FString InScreenShotPath,FVector2D OptionalAspectRatio);
	UFUNCTION(BlueprintCallable,BlueprintPure)
	bool IsRecording()const;
	//call back to read screenshot data
	void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
	FSlateApplication* SlateApplication;

	FVector2D AspectRatio;
	TArray<FColor> Colors;
	FIntRect CaptureRect;
	FString ScreenShotPath;
};

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VideoCaptureSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnError,FString,ErrorText);
class UCaptureSubsystemDirector;
/**
 * 
 */
USTRUCT(BlueprintType)
struct FVideoCaptureOptions
{
	// Out file name will be auto generated if empty
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString OutFileName;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString VideoFilter;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool UseGPU = true;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int FPS = 30;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int VideoBitRate = 800000;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float AudioDelay = 0.f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float SoundVolume = 1.f;
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool ShowUI = false;
	//Will do (height*x)/y to get aspect ratio (useful horizontal Cropping like instagram square shaped videos). set zero to use default aspect ratio 
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector2D OptionalCaptureAspectRatio;
	GENERATED_BODY()
};

UCLASS()
class CAPTURESUBSYSTEM_API UVideoCaptureSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()


public:
	UFUNCTION(BlueprintCallable)
	void StartCapture(FVideoCaptureOptions Options);
	UFUNCTION(BlueprintCallable)
	void EndCapture();


	UPROPERTY(Transient)
	UCaptureSubsystemDirector* Director = nullptr;


	// if path is empty it will be auto generated
	UFUNCTION(BlueprintCallable)
	void TakeScreenshot(FString InScreenShotPath, FVector2D OptionalAspectRatio);
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsRecording() const;

	UFUNCTION(BlueprintCallable, BlueprintPure)
	FString GetRecommendedVideoFileName() const;
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FString GetRecommendedPhotoFileName() const;
	//call back to read screenshot data
	void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer);
	UPROPERTY(BlueprintAssignable)
	FOnError OnError;
	
	FSlateApplication* SlateApplication;


	FString ScreenShotPath;
	FIntRect CaptureRect;
	FVector2D AspectRatio;
	TArray<FColor> Colors;
};

// Fill out your copyright notice in the Description page of Project Settings.


#include "VideoCaptureSubsystem.h"

#include "CaptureSubsystemDirector.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"


void UVideoCaptureSubsystem::StartCapture(FVideoCaptureOptions Options)
{
	Director = NewObject<UCaptureSubsystemDirector>(this);
	
	Director->Initialize_Director(GetWorld(),Options);
	
}

void UVideoCaptureSubsystem::EndCapture(FVideoCaptureOptions Options)
{
	if(Director)
	{
		if(GetWorld()->WorldType== EWorldType::Game)
			Director->EndWindowReader_StandardGame(nullptr);
#if WITH_EDITOR
		if(GetWorld()->WorldType == EWorldType::PIE)
			Director->EndWindowReader(false);
#endif
		Director=nullptr;
	}
	else
	{
		UE_LOG(LogTemp,Error,TEXT("End called but was not capturing "));
	}
}

void UVideoCaptureSubsystem::TakeScreenshot(FString InScreenShotPath,FVector2D OptionalAspectRatio)
{
	
	FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &UVideoCaptureSubsystem::OnBackBufferReady_RenderThread);
	SlateApplication=&FSlateApplication::Get();
	AspectRatio=OptionalAspectRatio;
		
		ScreenShotPath=InScreenShotPath;
	
	
		
	//FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Bitmap, CompressedBitmap);
	//bIsScreenshotSaved = FFileHelper::SaveArrayToFile(CompressedBitmap, *ScreenShotName);
	
}

bool UVideoCaptureSubsystem::IsRecording() const
{
	return  Director!=nullptr;
}


void UVideoCaptureSubsystem::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
	
	bool ScreenshotSuccess=false;


	
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();
	auto Texture=GEngine->GameViewport->Viewport->GetRenderTargetTexture();
	const int Crop=AspectRatio.IsZero()?Texture->GetSizeX():Texture->GetSizeY()*AspectRatio.X/AspectRatio.Y;
	const int Diff=Texture->GetSizeX()-Crop;
	CaptureRect = FIntRect(Diff/2, 0, Texture->GetSizeX()-Diff/2, Texture->GetSizeY());

	
	RHICmdList.ReadSurfaceData(Texture,CaptureRect,Colors,FReadSurfaceDataFlags());
	
	
	SlateApplication->GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);

	AsyncTask(ENamedThreads::BackgroundThreadPriority,[&,this]()
	{

				TArray<uint8> CompressedBitmap;
			IImageWrapperModule& ImageWrapperModule = FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
			bool bSuccess = ImageWrapper->SetRaw(Colors.GetData(), Colors.Num() * sizeof(FColor), CaptureRect.Width(), CaptureRect.Height(), ERGBFormat::BGRA, 8);
		if(bSuccess)
		{
			
			const TArray64<uint8>& Bytes = ImageWrapper->GetCompressed(0);
			
			bool bIsScreenshotSaved = FFileHelper::SaveArrayToFile(Bytes, *ScreenShotPath);
		}
	});
}

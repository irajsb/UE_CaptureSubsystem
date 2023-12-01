// Copyright iraj mohtasham aurelion.net 2023


#include "VideoCaptureSubsystem.h"

#include "CaptureGameViewportClient.h"
#include "CaptureSubsystemDirector.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Async/Async.h"
#include "CaptureSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/FileHelper.h"


void UVideoCaptureSubsystem::StartCapture(FVideoCaptureOptions Options)
{
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Capturing Video"));
	GEngine->GetGameUserSettings()->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	GEngine->GetGameUserSettings()->ApplySettings(false);
	
	Director = NewObject<UCaptureSubsystemDirector>(this);
	if (Options.OutFileName.IsEmpty())
	{
		Options.OutFileName = GetRecommendedVideoFileName();
	}
	//Check if file path is valid if not create the folders 
	if (!FPaths::FileExists(Options.OutFileName))
	{
		const FString FolderPath = FPaths::GetPath(Options.OutFileName);
		if (!FPaths::DirectoryExists(FolderPath))
		{
			FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FolderPath);
		}
	}
	Director->Initialize_Director(GetWorld(), Options,this);
}

void UVideoCaptureSubsystem::EndCapture()
{
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Ending Video"));
	if (Director)
	{
		if (GetWorld()->WorldType == EWorldType::Game)
		{
			Director->EndWindowReader_StandardGame(nullptr);
		}
#if WITH_EDITOR
		if (GetWorld()->WorldType == EWorldType::PIE)
		{
			Director->EndWindowReader(false);
		}
#endif
		Director = nullptr;
	}
	else
	{
		UE_LOG(LogCaptureSubsystem, Error, TEXT("End called but was not capturing "));
	}
}

void UVideoCaptureSubsystem::TakeScreenshot(FString InScreenShotPath, FVector2D OptionalAspectRatio)
{
	GEngine->GetGameUserSettings()->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	GEngine->GetGameUserSettings()->ApplySettings(false);
	UE_LOG(LogCaptureSubsystem, Log, TEXT("Begin Screenshot"));
	FString Path = InScreenShotPath.IsEmpty() ? GetRecommendedPhotoFileName() : InScreenShotPath;

	AspectRatio = OptionalAspectRatio;

	ScreenShotPath = Path;

	SlateApplication = &FSlateApplication::Get();
	SlateApplication->GetRenderer()->OnBackBufferReadyToPresent().AddUObject(
		this, &UVideoCaptureSubsystem::OnBackBufferReady_RenderThread);
}

bool UVideoCaptureSubsystem::IsRecording() const
{
	return Director != nullptr;
}

FString UVideoCaptureSubsystem::GetRecommendedVideoFileName() 
{
	const FDateTime Now = FDateTime::Now();
	return FString(FPlatformProcess::UserDir()) + "Capture/" + Now.ToString() + ".mp4";
}


FString UVideoCaptureSubsystem::GetRecommendedPhotoFileName() 
{
	const FDateTime Now = FDateTime::Now();
	return FString(FPlatformProcess::UserDir()) + "Capture/" + Now.ToString() + ".jpg";
}

void UVideoCaptureSubsystem::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& BackBuffer)
{
	const UCaptureGameViewportClient* ViewportClient = static_cast<UCaptureGameViewportClient*>(GetWorld()->
		GetGameViewport());
	if (!ViewportClient)
	{
		UE_LOG(LogCaptureSubsystem, Error, TEXT("Please set the viewport client "));
		return;
	}
	FRHICommandListImmediate& RHICmdList = GRHICommandList.GetImmediateCommandList();

	if (const auto Texture = ViewportClient->MyRenderTarget->GetResource()->GetTexture2DRHI())
	{
		const int Crop = AspectRatio.IsZero()
			                 ? Texture->GetSizeX()
			                 : Texture->GetSizeY() * AspectRatio.X / AspectRatio.Y;
		const int Diff = Texture->GetSizeX() - Crop;
		CaptureRect = FIntRect(Diff / 2, 0, Texture->GetSizeX() - Diff / 2, Texture->GetSizeY());


		RHICmdList.ReadSurfaceData(Texture, CaptureRect, Colors, FReadSurfaceDataFlags());


		SlateApplication->GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);

		AsyncTask(ENamedThreads::GameThread, [&,this]()
		{
			TArray<uint8> CompressedBitmap;
			IImageWrapperModule& ImageWrapperModule = FModuleManager::GetModuleChecked<IImageWrapperModule>(
				TEXT("ImageWrapper"));
			const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
			if (ImageWrapper->SetRaw(Colors.GetData(), Colors.Num() * sizeof(FColor), CaptureRect.Width(),
			                         CaptureRect.Height(), ERGBFormat::BGRA, 8))
			{
				const TArray64<uint8>& Bytes = ImageWrapper->GetCompressed(0);

				const bool bIsScreenshotSaved = FFileHelper::SaveArrayToFile(Bytes, *ScreenShotPath);
				if (!bIsScreenshotSaved)
				{
					UE_LOG(LogCaptureSubsystem, Error, TEXT("Screenshot Save fail"));
				}
			}
			else
			{
				UE_LOG(LogCaptureSubsystem, Error, TEXT("Image data invalid"))
			}
		});
	}
	else
	{
		UE_LOG(LogCaptureSubsystem, Error, TEXT("Texture invalid "));
	}
}

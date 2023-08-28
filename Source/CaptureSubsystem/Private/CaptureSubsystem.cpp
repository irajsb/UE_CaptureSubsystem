// Copyright iraj mohtasham aurelion.net 2023

#include "CaptureSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "Core.h"
extern  "C" {
#include "libavformat/avformat.h"
}
DEFINE_LOG_CATEGORY(LogCaptureSubsystem);

#define LOCTEXT_NAMESPACE "FCaptureSubsystemModule"

void FCaptureSubsystemModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	
	AVUtilLibrary = LoadLibrary(TEXT("avutil"), TEXT("57"));
	SWResampleLibrary = LoadLibrary(TEXT("swresample"), TEXT("4"));
	AVCodecLibrary = LoadLibrary(TEXT("avcodec"), TEXT("59"));
	AVFormatLibrary = LoadLibrary(TEXT("avformat"), TEXT("59"));
	SWScaleLibrary = LoadLibrary(TEXT("swscale"), TEXT("6"));
	PostProcLibrary = LoadLibrary(TEXT("postproc"), TEXT("56"));
	AVFilterLibrary = LoadLibrary(TEXT("avfilter"), TEXT("8"));
	AVDeviceLibrary = LoadLibrary(TEXT("avdevice"), TEXT("59"));
	Initialized = true;
}

void FCaptureSubsystemModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	if (!Initialized)
	{
		return;
	}
	if (AVDeviceLibrary) FPlatformProcess::FreeDllHandle(AVDeviceLibrary);
	if (AVFilterLibrary) FPlatformProcess::FreeDllHandle(AVFilterLibrary);
	if (PostProcLibrary) FPlatformProcess::FreeDllHandle(PostProcLibrary);
	if (SWScaleLibrary) FPlatformProcess::FreeDllHandle(SWScaleLibrary);
	if (AVFormatLibrary) FPlatformProcess::FreeDllHandle(AVFormatLibrary);
	if (AVCodecLibrary) FPlatformProcess::FreeDllHandle(AVCodecLibrary);
	if (SWResampleLibrary) FPlatformProcess::FreeDllHandle(SWResampleLibrary);
	if (AVUtilLibrary) FPlatformProcess::FreeDllHandle(AVUtilLibrary);

	Initialized = false;
}

void* FCaptureSubsystemModule::LoadLibrary(const FString& name, const FString& version)
{
	FString BaseDir = IPluginManager::Get().FindPlugin("CaptureSubsystem")->GetBaseDir();

	FString LibDir;
	FString extension;
	FString prefix;
	FString separator;
#if PLATFORM_MAC
	LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/ffmpeg/lib/osx"));
	extension = TEXT(".dylib");
	prefix = "lib";
	separator = ".";
#elif PLATFORM_WINDOWS
	extension = TEXT(".dll");
	prefix = "";
	separator = "-";

	LibDir = FPaths::Combine(*BaseDir, TEXT("ThirdParty/ffmpeg/bin/vs/x64"));

#endif
	if (!LibDir.IsEmpty()) {
		FString LibraryPath = FPaths::Combine(*LibDir, prefix + name + separator + version + extension);
		UE_LOG(LogCaptureSubsystem,Log,TEXT("Loading %s"),*LibraryPath)
		return FPlatformProcess::GetDllHandle(*LibraryPath);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCaptureSubsystemModule, CaptureSubsystem)
// Copyright iraj mohtasham aurelion.net 2023

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
DECLARE_LOG_CATEGORY_EXTERN(LogCaptureSubsystem, Log, All);
class FCaptureSubsystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;


private:
	void* LoadLibrary(const  FString& name, const FString& version);
	void* AVUtilLibrary;
	void* SWResampleLibrary;
	void* AVCodecLibrary;
	void* SWScaleLibrary;
	void* AVFormatLibrary;
	void* PostProcLibrary;
	void* AVFilterLibrary;
	void* AVDeviceLibrary;

	bool Initialized;
};

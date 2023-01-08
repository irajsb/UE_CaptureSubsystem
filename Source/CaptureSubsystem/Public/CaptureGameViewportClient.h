// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "CaptureGameViewportClient.generated.h"

/**
 * 
 */
UCLASS()
class CAPTURESUBSYSTEM_API UCaptureGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

	
	virtual void Draw(FViewport* InViewport, FCanvas* Canvas) override;

	void CopyViewportToRenderTarget(UTextureRenderTarget2D* RenderTarget);

public:
	UPROPERTY()
	UTextureRenderTarget2D* MyRenderTarget;
	
	bool PendingCopyViewportToRenderTarget=true;


	void CopyTextureRHI(FRenderTarget* MyViewRenderTarget, UTextureRenderTarget2D* DestRenderTarget);
};

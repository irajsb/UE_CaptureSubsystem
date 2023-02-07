// Fill out your copyright notice in the Description page of Project Settings.


#include "CaptureGameViewportClient.h"

#include "CaptureSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"


void UCaptureGameViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	Super::Draw(Viewport, Canvas);

	if (!MyRenderTarget)
	{
		MyRenderTarget = NewObject<UTextureRenderTarget2D>(GetWorld());
		check(MyRenderTarget);
		MyRenderTarget->RenderTargetFormat = RTF_RGB10A2;
		MyRenderTarget->ClearColor = FLinearColor::Black;
		MyRenderTarget->bAutoGenerateMips = false;
		MyRenderTarget->InitAutoFormat(Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y);
		MyRenderTarget->UpdateResourceImmediate(true);
	}
	//Check for viewport resize
	if (InViewport->GetSizeXY() != FIntPoint(MyRenderTarget->SizeX, MyRenderTarget->SizeY))
	{
		UE_LOG(LogCaptureSubsystem, Warning, TEXT("Viewport resize detected Resizing render targets"))
		MyRenderTarget->ResizeTarget(Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y);
	}

	if (InViewport && PendingCopyViewportToRenderTarget)
	{
		CopyTextureRHI(InViewport, MyRenderTarget);
	}
}

void UCaptureGameViewportClient::CopyViewportToRenderTarget(UTextureRenderTarget2D* RenderTarget)
{
	MyRenderTarget = RenderTarget;
	PendingCopyViewportToRenderTarget = true;
	
}

void UCaptureGameViewportClient::CopyTextureRHI(FRenderTarget* MyViewRenderTarget,
                                                UTextureRenderTarget2D* DestRenderTarget) const
{
	ENQUEUE_RENDER_COMMAND(CopyToResolveTarget)(
		[MyViewRenderTarget, DestRenderTarget](FRHICommandListImmediate& RHICmdList)
		{
			const FTexture2DRHIRef destTexture = DestRenderTarget->GetRenderTargetResource()->GetRenderTargetTexture();
			const FResolveParams ResolveParams;
			RHICmdList.CopyToResolveTarget(MyViewRenderTarget->GetRenderTargetTexture(), destTexture, ResolveParams);
		});
	FlushRenderingCommands();
}

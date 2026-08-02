#include "FolioMaskShader.h"

#include "FolioInsights.h"

#include "Engine/TextureRenderTarget2D.h"
#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_GLOBAL_SHADER(FFolioMaskPS, "/Folio/FolioMask.usf", "MainPS", SF_Pixel);

namespace Folio
{
	void ApplyMask(UTextureRenderTarget2D* Target, UTextureRenderTarget2D* Mask, const FMaskOptions& Options)
	{
		if (!Target || !Mask)
		{
			return;
		}

		FTextureRenderTargetResource* TargetResource = Target->GameThread_GetRenderTargetResource();
		FTextureRenderTargetResource* MaskResource = Mask->GameThread_GetRenderTargetResource();

		if (!TargetResource || !MaskResource)
		{
			return;
		}

		const FIntPoint Size(Target->SizeX, Target->SizeY);
		const FVector4f Parameters(Options.bUseAlpha ? 1.0f : 0.0f, Options.bInvert ? 1.0f : 0.0f, 0.0f, 0.0f);

		ENQUEUE_RENDER_COMMAND(FolioMask)(
			[TargetResource, MaskResource, Size, Parameters](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Folio_Mask);
				SCOPE_CYCLE_COUNTER(STAT_FolioMaskFilter);

				FRDGBuilder GraphBuilder(RHICmdList);
				// Braced: the scope has to close before Execute, which asserts on an open breadcrumb.
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioEffects, "FolioEffects");

					FRDGTextureRef Colour = RegisterExternalTexture(GraphBuilder,
						TargetResource->GetRenderTargetTexture(), TEXT("FolioLayer"));
					FRDGTextureRef MaskTexture = RegisterExternalTexture(GraphBuilder,
						MaskResource->GetRenderTargetTexture(), TEXT("FolioMaskLayer"));

					// A pass cannot read and write the same texture, so the masked result lands in a
					// scratch and is copied back - the same shape the colour filters use.
					const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8,
						FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource);
					FRDGTextureRef Scratch = GraphBuilder.CreateTexture(ScratchDesc, TEXT("FolioMaskScratch"));

					FFolioMaskPS::FParameters* Pass = GraphBuilder.AllocParameters<FFolioMaskPS::FParameters>();
					Pass->SourceTexture = Colour;
					Pass->SourceSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					Pass->MaskTexture = MaskTexture;
					// Bilinear on the mask alone: it may have been drawn at a different scale to what it
					// masks, and a point sample there is what makes a soft mask look stepped.
					Pass->MaskSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					Pass->MaskParameters = Parameters;
					Pass->InvSize = FVector2f(1.0f / Size.X, 1.0f / Size.Y);
					Pass->RenderTargets[0] = FRenderTargetBinding(Scratch, ERenderTargetLoadAction::ENoAction);

					const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FFolioMaskPS> PixelShader(ShaderMap);

					FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioMask"),
						PixelShader, Pass, FIntRect(0, 0, Size.X, Size.Y));

					AddCopyTexturePass(GraphBuilder, Scratch, Colour);
				}

				GraphBuilder.Execute();
			});
	}
}

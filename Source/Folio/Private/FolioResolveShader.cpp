#include "FolioResolveShader.h"

#include "FolioInsights.h"

#include "Engine/TextureRenderTarget2D.h"
#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_GLOBAL_SHADER(FFolioResolvePS, "/Folio/FolioResolve.usf", "MainPS", SF_Pixel);

namespace Folio
{
	void ResolveSupersample(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Destination,
		const int32 Samples)
	{
		if (!Source || !Destination || Samples < 2)
		{
			return;
		}

		FTextureRenderTargetResource* SourceResource = Source->GameThread_GetRenderTargetResource();
		FTextureRenderTargetResource* DestinationResource = Destination->GameThread_GetRenderTargetResource();

		if (!SourceResource || !DestinationResource)
		{
			return;
		}

		const FIntPoint SourceSize(Source->SizeX, Source->SizeY);
		const FIntPoint Size(Destination->SizeX, Destination->SizeY);

		ENQUEUE_RENDER_COMMAND(FolioResolve)(
			[SourceResource, DestinationResource, SourceSize, Size, Samples]
			(FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Folio_ResolveSupersample);
				SCOPE_CYCLE_COUNTER(STAT_FolioSupersample);

				FRDGBuilder GraphBuilder(RHICmdList);

				// Braced: the scope has to close before Execute, which asserts on an open breadcrumb.
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioSupersample, "FolioSupersample");

					FRDGTextureRef SourceTexture = RegisterExternalTexture(GraphBuilder,
						SourceResource->GetRenderTargetTexture(), TEXT("FolioSupersampled"));
					FRDGTextureRef DestinationTexture = RegisterExternalTexture(GraphBuilder,
						DestinationResource->GetRenderTargetTexture(), TEXT("FolioResolved"));

					FFolioResolvePS::FParameters* Pass = GraphBuilder.AllocParameters<FFolioResolvePS::FParameters>();
					Pass->SourceTexture = SourceTexture;
					Pass->SourceSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					Pass->ResolveParameters = FVector4f(float(Samples),
						1.0f / float(SourceSize.X), 1.0f / float(SourceSize.Y), 0.0f);
					Pass->InvSize = FVector2f(1.0f / Size.X, 1.0f / Size.Y);
					Pass->RenderTargets[0] = FRenderTargetBinding(DestinationTexture,
						ERenderTargetLoadAction::ENoAction);

					const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FFolioResolvePS> PixelShader(ShaderMap);

					FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioResolve"),
						PixelShader, Pass, FIntRect(0, 0, Size.X, Size.Y));
				}

				GraphBuilder.Execute();
			});
	}
}

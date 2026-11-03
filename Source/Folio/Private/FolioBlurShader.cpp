#include "FolioBlurShader.h"

#include "FolioInsights.h"
#include "FolioLayerPasses.h"

#include "Engine/TextureRenderTarget2D.h"
#include "FolioModule.h"
#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_GLOBAL_SHADER(FFolioBlurPS, "/Folio/FolioBlur.usf", "MainPS", SF_Pixel);

namespace
{
	/** A wider kernel than this stops being visibly different and starts being expensive. */
	constexpr int32 MaxTaps = 32;

	void AddBlurPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Source, FRDGTextureRef Destination,
		const FVector2f Direction, const float Sigma, const FIntPoint Size, float Step, const float Extent)
	{
		// A kernel cut off short of its extent ends in a visible edge, so past the tap cap the taps spread
		// out rather than stopping early.
		Step = FMath::Max(Step, Sigma * Extent / MaxTaps);

		// The shader works in taps rather than in pixels, so a step is expressed by widening what one
		// tap covers and narrowing sigma to match. The two cancel: the same curve over the same
		// distance, sampled less finely.
		const float TapSigma = Sigma / FMath::Max(Step, 1.0f);
		const int32 Taps = FMath::Clamp(FMath::CeilToInt(TapSigma * Extent), 1, MaxTaps);

		FFolioBlurPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFolioBlurPS::FParameters>();
		// The RDG texture goes in as an RDG parameter. Reaching for GetRHI() here is what the
		// bAllowRHIAccess assertion catches: outside a pass the resource has no RHI yet.
		Parameters->SourceTexture = Source;
		Parameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->BlurParameters = FVector4f(
			Direction.X * FMath::Max(Step, 1.0f) / static_cast<float>(Size.X),
			Direction.Y * FMath::Max(Step, 1.0f) / static_cast<float>(Size.Y),
			TapSigma,
			static_cast<float>(Taps));
		Parameters->InvSize = FVector2f(1.0f / Size.X, 1.0f / Size.Y);
		Parameters->RenderTargets[0] = FRenderTargetBinding(Destination, ERenderTargetLoadAction::ENoAction);

		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FFolioBlurPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioBlur"),
			PixelShader, Parameters, FIntRect(0, 0, Size.X, Size.Y));
	}
}

namespace Folio
{
	void AddBlurPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, const float Sigma, const float Step,
		const float Extent)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Folio_AddBlurPasses);

		if (!Texture || Sigma <= 0.0f)
		{
			return;
		}

		const FIntPoint Size = Texture->Desc.Extent;

		// Separable: horizontal into a scratch target, vertical back into the texture.
		FRDGTextureRef Scratch = GraphBuilder.CreateTexture(ScratchDescFor(Texture), TEXT("FolioBlurScratch"));

		AddBlurPass(GraphBuilder, Texture, Scratch, FVector2f(1.0f, 0.0f), Sigma, Size, Step, Extent);
		AddBlurPass(GraphBuilder, Scratch, Texture, FVector2f(0.0f, 1.0f), Sigma, Size, Step, Extent);
	}

	void BlurRenderTarget(UTextureRenderTarget2D* Target, const float Sigma, const float Step)
	{
		if (!Target || Sigma <= 0.0f)
		{
			return;
		}

		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(FolioBlur)(
			[Resource, Sigma, Step](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Folio_Blur);
				SCOPE_CYCLE_COUNTER(STAT_FolioBlur);

				FRDGBuilder GraphBuilder(RHICmdList);
				// Braced: the scope has to close before Execute, which asserts on an open breadcrumb.
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioEffects, "FolioEffects");

					FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder,
						Resource->GetRenderTargetTexture(), TEXT("FolioLayer"));
					AddBlurPasses(GraphBuilder, TargetTexture, Sigma, Step);
				}

				GraphBuilder.Execute();
			});
	}
}

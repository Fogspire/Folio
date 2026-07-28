#include "FolioBlurShader.h"

#include "Engine/TextureRenderTarget2D.h"
#include "FolioModule.h"
#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_GLOBAL_SHADER(FFolioBlurVS, "/Folio/FolioBlur.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FFolioBlurPS, "/Folio/FolioBlur.usf", "MainPS", SF_Pixel);

namespace
{
	/** A wider kernel than this stops being visibly different and starts being expensive. */
	constexpr int32 MaxTaps = 32;

	void AddBlurPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Source, FRDGTextureRef Destination,
		const FVector2f Direction, const float Sigma, const FIntPoint Size)
	{
		const int32 Taps = FMath::Clamp(FMath::CeilToInt(Sigma * 2.0f), 1, MaxTaps);

		FFolioBlurPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFolioBlurPS::FParameters>();
		// The RDG texture goes in as an RDG parameter. Reaching for GetRHI() here is what the
		// bAllowRHIAccess assertion catches: outside a pass the resource has no RHI yet.
		Parameters->SourceTexture = Source;
		Parameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->BlurParameters = FVector4f(
			Direction.X / static_cast<float>(Size.X),
			Direction.Y / static_cast<float>(Size.Y),
			Sigma,
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
	void BlurRenderTarget(UTextureRenderTarget2D* Target, const float Sigma)
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

		const FIntPoint Size(Target->SizeX, Target->SizeY);

		ENQUEUE_RENDER_COMMAND(FolioBlur)(
			[Resource, Size, Sigma](FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder GraphBuilder(RHICmdList);

				FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder,
					Resource->GetRenderTargetTexture(), TEXT("FolioLayer"));

				// Separable: horizontal into a scratch target, vertical back into the layer.
				const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8,
					FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource);
				FRDGTextureRef Scratch = GraphBuilder.CreateTexture(ScratchDesc, TEXT("FolioBlurScratch"));

				AddBlurPass(GraphBuilder, TargetTexture, Scratch, FVector2f(1.0f, 0.0f), Sigma, Size);
				AddBlurPass(GraphBuilder, Scratch, TargetTexture, FVector2f(0.0f, 1.0f), Sigma, Size);

				GraphBuilder.Execute();
			});
	}
}

#include "FolioColourShader.h"

#include "FolioInsights.h"

#include "Engine/TextureRenderTarget2D.h"
#include "GlobalRenderResources.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

IMPLEMENT_GLOBAL_SHADER(FFolioColourPS, "/Folio/FolioColour.usf", "MainPS", SF_Pixel);

namespace
{
	void AddColourPass(FRDGBuilder& GraphBuilder, FRDGTextureRef Source, FRDGTextureRef Destination,
		const Folio::FColourFilter& Filter, const FIntPoint Size)
	{
		FFolioColourPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFolioColourPS::FParameters>();
		Parameters->SourceTexture = Source;
		// Point rather than bilinear: this is a one-to-one pass, and filtering would soften every
		// edge the layer already resolved for no gain.
		Parameters->SourceSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->ColourParameters = FVector4f(float(Filter.Kind), Filter.Amount, 0.0f, 0.0f);
		Parameters->InvSize = FVector2f(1.0f / Size.X, 1.0f / Size.Y);
		Parameters->RenderTargets[0] = FRenderTargetBinding(Destination, ERenderTargetLoadAction::ENoAction);

		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FFolioColourPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioColour"),
			PixelShader, Parameters, FIntRect(0, 0, Size.X, Size.Y));
	}
}

namespace Folio
{
	void ApplyColourFilters(UTextureRenderTarget2D* Target, const TArrayView<const FColourFilter> Filters)
	{
		if (!Target || Filters.IsEmpty())
		{
			return;
		}

		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return;
		}

		const FIntPoint Size(Target->SizeX, Target->SizeY);
		TArray<FColourFilter> Ordered(Filters.GetData(), Filters.Num());

		ENQUEUE_RENDER_COMMAND(FolioColour)(
			[Resource, Size, Ordered = MoveTemp(Ordered)](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Folio_ColourFilter);
				SCOPE_CYCLE_COUNTER(STAT_FolioColourFilter);

				FRDGBuilder GraphBuilder(RHICmdList);
				// Braced: the scope has to close before Execute, which asserts on an open breadcrumb.
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioEffects, "FolioEffects");

					FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder,
						Resource->GetRenderTargetTexture(), TEXT("FolioLayer"));

					const FRDGTextureDesc ScratchDesc = FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8,
						FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource);
					FRDGTextureRef Scratch = GraphBuilder.CreateTexture(ScratchDesc, TEXT("FolioColourScratch"));

					// Ping-ponged, because a pass cannot read and write the same texture. An odd number of
					// filters would otherwise leave the result in the scratch target, so the last pass is
					// followed by a copy back when the count is odd.
					FRDGTextureRef Read = TargetTexture;
					FRDGTextureRef Write = Scratch;

					for (const FColourFilter& Filter : Ordered)
					{
						AddColourPass(GraphBuilder, Read, Write, Filter, Size);
						Swap(Read, Write);
					}

					if (Read != TargetTexture)
					{
						AddCopyTexturePass(GraphBuilder, Read, TargetTexture);
					}
				}

				GraphBuilder.Execute();
			});
	}
}

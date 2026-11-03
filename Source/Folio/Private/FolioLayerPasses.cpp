#include "FolioLayerPasses.h"

#include "FolioBlurShader.h"
#include "FolioCore/FolioShaderFilters.h"
#include "FolioInsights.h"
#include "FolioModule.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Misc/ScopeRWLock.h"
#include "PixelShaderUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/DrawElements.h"
#include "TextureResource.h"

IMPLEMENT_GLOBAL_SHADER(FFolioGlowPS, "/Folio/FolioGlow.usf", "MainPS", SF_Pixel);

namespace
{
	FRWLock RegistryLock;

	TMap<FName, TSharedRef<IFolioShaderFilter, ESPMode::ThreadSafe>>& Registry()
	{
		static TMap<FName, TSharedRef<IFolioShaderFilter, ESPMode::ThreadSafe>> Filters;
		return Filters;
	}

	float LargestLength(TConstArrayView<FFolioFilterArg> Args)
	{
		float Largest = 0.0f;
		for (const FFolioFilterArg& Arg : Args)
		{
			if (Arg.Kind == FFolioFilterArg::EKind::Length)
			{
				Largest = FMath::Max(Largest, FMath::Abs(Arg.Value));
			}
		}
		return Largest;
	}

	void AddHaloPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Source, FRDGTextureRef Destination,
		const Folio::FLayerPass& Pass, const float PixelScale)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Folio_AddHaloPasses);

		const FIntPoint Size = Source->Desc.Extent;
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FFolioGlowPS> PixelShader(ShaderMap);

		const FLinearColor Straight = Pass.Colour;
		const FVector4f Premultiplied(Straight.R * Straight.A, Straight.G * Straight.A, Straight.B * Straight.A, Straight.A);
		const FVector2f OffsetUV(Pass.Offset.X * PixelScale / Size.X, Pass.Offset.Y * PixelScale / Size.Y);
		const FVector2f InvSize(1.0f / Size.X, 1.0f / Size.Y);

		FRDGTextureRef Halo = GraphBuilder.CreateTexture(Folio::ScratchDescFor(Source), TEXT("FolioHalo"));

		{
			FFolioGlowPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFolioGlowPS::FParameters>();
			Parameters->SourceTexture = Source;
			Parameters->HaloTexture = Source;
			Parameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->GlowColour = Premultiplied;
			Parameters->GlowParameters = FVector4f(OffsetUV.X, OffsetUV.Y, Pass.Strength, 0.0f);
			Parameters->InvSize = InvSize;
			Parameters->RenderTargets[0] = FRenderTargetBinding(Halo, ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioHaloShape"),
				PixelShader, Parameters, FIntRect(0, 0, Size.X, Size.Y));
		}

		// Four sigmas: the halo is blended in linear light, where the tail past three is still a visible step.
		Folio::AddBlurPasses(GraphBuilder, Halo, Pass.Sigma * PixelScale, PixelScale, /*Extent*/ 4.0f);

		{
			FFolioGlowPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFolioGlowPS::FParameters>();
			Parameters->SourceTexture = Source;
			Parameters->HaloTexture = Halo;
			Parameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->GlowColour = Premultiplied;
			Parameters->GlowParameters = FVector4f(OffsetUV.X, OffsetUV.Y, Pass.Strength, 1.0f);
			Parameters->InvSize = InvSize;
			Parameters->RenderTargets[0] = FRenderTargetBinding(Destination, ERenderTargetLoadAction::ENoAction);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioHaloComposite"),
				PixelShader, Parameters, FIntRect(0, 0, Size.X, Size.Y));
		}
	}

	/** Records every pass over Texture, leaving the result in Texture. */
	void AddLayerPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture, TConstArrayView<Folio::FLayerPass> Passes,
		const float PixelScale, const float Time, const bool bBackdrop)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Folio_AddLayerPasses);

		FRDGTextureRef Current = Texture;
		for (const Folio::FLayerPass& Pass : Passes)
		{
			if (Pass.Kind == Folio::FLayerPass::EKind::Shader && !Pass.Filter.IsValid())
			{
				continue;
			}

			FRDGTextureRef Next = GraphBuilder.CreateTexture(Folio::ScratchDescFor(Texture), TEXT("FolioLayerPass"));

			if (Pass.Kind == Folio::FLayerPass::EKind::Halo)
			{
				AddHaloPasses(GraphBuilder, Current, Next, Pass, PixelScale);
			}
			else
			{
				RDG_EVENT_SCOPE(GraphBuilder, "FolioShaderFilter %s", *Pass.Name.ToString());

				FFolioShaderFilterInputs Inputs;
				Inputs.Input = Current;
				Inputs.Output = Next;
				Inputs.Size = Texture->Desc.Extent;
				Inputs.PixelScale = PixelScale;
				Inputs.Time = Time;
				Inputs.bBackdrop = bBackdrop;
				Inputs.Args = Pass.Args;
				Pass.Filter->AddPasses(GraphBuilder, Inputs);
			}

			Current = Next;
		}

		if (Current != Texture)
		{
			AddCopyTexturePass(GraphBuilder, Current, Texture);
		}
	}

	/** backdrop-filter: shader(...), drawn at its place in Slate's order. */
	class FBackdropElement final : public ICustomSlateElement
	{
	public:
		FBackdropElement(const FIntRect& InRect, TArray<Folio::FLayerPass>&& InPasses, const float InPixelScale,
			const float InTime)
			: Rect(InRect), Passes(MoveTemp(InPasses)), PixelScale(InPixelScale), Time(InTime)
		{
		}

		virtual void Draw_RenderThread(FRDGBuilder& GraphBuilder, const FDrawPassInputs& Inputs) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Folio_BackdropPasses);

			FRDGTextureRef Output = Inputs.OutputTexture;
			if (!Output)
			{
				return;
			}

			FIntRect Region = Rect + FIntPoint(FMath::RoundToInt(Inputs.ElementsOffset.X), FMath::RoundToInt(Inputs.ElementsOffset.Y));
			Region.Clip(FIntRect(FIntPoint::ZeroValue, Output->Desc.Extent));
			if (Region.Area() <= 0)
			{
				return;
			}

			RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioEffects, "FolioEffects");

			FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Region.Size(), Output->Desc.Format,
				FClearValueBinding::Transparent,
				TexCreate_RenderTargetable | TexCreate_ShaderResource | (Output->Desc.Flags & TexCreate_SRGB));
			FRDGTextureRef Copy = GraphBuilder.CreateTexture(Desc, TEXT("FolioBackdrop"));

			FRHICopyTextureInfo In;
			In.SourcePosition = FIntVector(Region.Min.X, Region.Min.Y, 0);
			In.Size = FIntVector(Region.Width(), Region.Height(), 1);
			AddCopyTexturePass(GraphBuilder, Output, Copy, In);

			AddLayerPasses(GraphBuilder, Copy, Passes, PixelScale, Time, /*bBackdrop*/ true);

			FRHICopyTextureInfo Out;
			Out.DestPosition = FIntVector(Region.Min.X, Region.Min.Y, 0);
			Out.Size = In.Size;
			AddCopyTexturePass(GraphBuilder, Copy, Output, Out);
		}

	private:
		FIntRect Rect;
		TArray<Folio::FLayerPass> Passes;
		float PixelScale = 1.0f;
		float Time = 0.0f;
	};
}

float IFolioShaderFilter::GetReach(const TConstArrayView<FFolioFilterArg> Args) const
{
	return 3.0f * LargestLength(Args);
}

namespace Folio
{
	TArray<FFolioFilterArg> ToFilterArgs(const std::vector<FShaderFilterArg>& Args)
	{
		TArray<FFolioFilterArg> Out;
		Out.Reserve(int32(Args.size()));
		for (const FShaderFilterArg& Arg : Args)
		{
			FFolioFilterArg& Converted = Out.AddDefaulted_GetRef();
			Converted.Value = Arg.Value;
			switch (Arg.Kind)
			{
			case FShaderFilterArg::EKind::Number: Converted.Kind = FFolioFilterArg::EKind::Number; break;
			case FShaderFilterArg::EKind::Length: Converted.Kind = FFolioFilterArg::EKind::Length; break;
			case FShaderFilterArg::EKind::Colour:
				Converted.Kind = FFolioFilterArg::EKind::Colour;
				Converted.Colour = FLinearColor(Arg.Colour[0], Arg.Colour[1], Arg.Colour[2], Arg.Colour[3]);
				break;
			case FShaderFilterArg::EKind::Word:
				Converted.Kind = FFolioFilterArg::EKind::Word;
				Converted.Word = FName(UTF8_TO_TCHAR(Arg.Word.c_str()));
				break;
			}
		}
		return Out;
	}

	void RegisterShaderFilter(const FName Name, TSharedRef<IFolioShaderFilter, ESPMode::ThreadSafe> Filter)
	{
		{
			FWriteScopeLock Lock(RegistryLock);
			Registry().Add(Name, Filter);
		}

		// The parser grows the element's layer by this, so it needs the pass's own answer.
		const TWeakPtr<IFolioShaderFilter, ESPMode::ThreadSafe> Weak = Filter;
		SetShaderFilterReach(TCHAR_TO_UTF8(*Name.ToString()), [Weak](const std::vector<FShaderFilterArg>& Args)
		{
			const TSharedPtr<IFolioShaderFilter, ESPMode::ThreadSafe> Pinned = Weak.Pin();
			return Pinned.IsValid() ? Pinned->GetReach(ToFilterArgs(Args)) : 0.0f;
		});
	}

	void UnregisterShaderFilter(const FName Name)
	{
		{
			FWriteScopeLock Lock(RegistryLock);
			Registry().Remove(Name);
		}
		ClearShaderFilterReach(TCHAR_TO_UTF8(*Name.ToString()));
	}

	TSharedPtr<IFolioShaderFilter, ESPMode::ThreadSafe> FindShaderFilter(const FName Name)
	{
		FReadScopeLock Lock(RegistryLock);
		const TSharedRef<IFolioShaderFilter, ESPMode::ThreadSafe>* Found = Registry().Find(Name);
		return Found ? TSharedPtr<IFolioShaderFilter, ESPMode::ThreadSafe>(*Found) : nullptr;
	}

	FRDGTextureDesc ScratchDescFor(FRDGTextureRef Texture)
	{
		// The colour space comes along: an sRGB layer ping-ponged through a linear 8-bit scratch loses
		// most of its dark values on the way, which shows as banding in a soft shadow or glow.
		return FRDGTextureDesc::Create2D(Texture->Desc.Extent, Texture->Desc.Format, FClearValueBinding::Transparent,
			TexCreate_RenderTargetable | TexCreate_ShaderResource | (Texture->Desc.Flags & TexCreate_SRGB));
	}

	float FLayerPass::Reach() const
	{
		if (Kind == EKind::Halo)
		{
			return 4.0f * Sigma + FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y));
		}
		return Filter.IsValid() ? Filter->GetReach(Args) : 0.0f;
	}

	bool FLayerPass::IsAnimated() const
	{
		return Kind == EKind::Shader && Filter.IsValid() && Filter->IsAnimated(Args);
	}

	uint32 FLayerPass::Hash() const
	{
		uint32 Seed = GetTypeHash(uint8(Kind));
		if (Kind == EKind::Halo)
		{
			Seed = HashCombineFast(Seed, GetTypeHash(Colour));
			Seed = HashCombineFast(Seed, GetTypeHash(Offset));
			Seed = HashCombineFast(Seed, GetTypeHash(Sigma));
			return HashCombineFast(Seed, GetTypeHash(Strength));
		}

		Seed = HashCombineFast(Seed, GetTypeHash(Name));
		Seed = HashCombineFast(Seed, GetTypeHash(Filter.Get()));
		for (const FFolioFilterArg& Arg : Args)
		{
			Seed = HashCombineFast(Seed, GetTypeHash(uint8(Arg.Kind)));
			Seed = HashCombineFast(Seed, GetTypeHash(Arg.Value));
			Seed = HashCombineFast(Seed, GetTypeHash(Arg.Colour));
			Seed = HashCombineFast(Seed, GetTypeHash(Arg.Word));
		}
		return Seed;
	}

	float LayerPassTime()
	{
		return float(FPlatformTime::Seconds() - GStartTime);
	}

	void ApplyLayerPasses(UTextureRenderTarget2D* Target, const TConstArrayView<FLayerPass> Passes, const float PixelScale)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Folio_ApplyLayerPasses);

		if (!Target || Passes.IsEmpty())
		{
			return;
		}

		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(FolioLayerPasses)(
			[Resource, Copied = TArray<FLayerPass>(Passes), PixelScale, Time = LayerPassTime()](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(Folio_LayerPasses);
				SCOPE_CYCLE_COUNTER(STAT_FolioLayerPasses);

				FRDGBuilder GraphBuilder(RHICmdList);
				// Braced: the scope has to close before Execute, which asserts on an open breadcrumb.
				{
					RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioEffects, "FolioEffects");

					FRDGTextureRef Texture = RegisterExternalTexture(GraphBuilder,
						Resource->GetRenderTargetTexture(), TEXT("FolioLayer"));
					AddLayerPasses(GraphBuilder, Texture, Copied, PixelScale, Time, /*bBackdrop*/ false);
				}

				GraphBuilder.Execute();
			});
	}

	TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> DrawBackdropPasses(FSlateWindowElementList& ElementList,
		const int32 LayerId, const FIntRect& WindowRect, TArray<FLayerPass> Passes, const float PixelScale)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Folio_DrawBackdropPasses);

		if (WindowRect.Area() <= 0 || Passes.IsEmpty())
		{
			return nullptr;
		}

		TSharedPtr<ICustomSlateElement, ESPMode::ThreadSafe> Element =
			MakeShared<FBackdropElement, ESPMode::ThreadSafe>(WindowRect, MoveTemp(Passes), PixelScale, LayerPassTime());
		FSlateDrawElement::MakeCustom(ElementList, LayerId, Element);
		return Element;
	}
}

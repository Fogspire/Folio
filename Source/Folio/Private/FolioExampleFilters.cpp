#include "FolioShaderFilters.h"

#include "FolioModule.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"

/** The pixel shader behind the `heat` example. */
class FFolioHeatPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioHeatPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioHeatPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER(FVector2f, InvSize)
		SHADER_PARAMETER(FVector4f, HeatParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFolioHeatPS, "/Folio/FolioHeat.usf", "MainPS", SF_Pixel);

namespace
{
	/**
	 * `filter: shader("heat <amplitude> <ripples> <speed>")`. The reference for a game's own pass.
	 *
	 * Reads its arguments by position, reaches as far as its amplitude, and animates, so its layer is
	 * redrawn every frame rather than cached.
	 */
	class FHeatFilter final : public IFolioShaderFilter
	{
	public:
		virtual void AddPasses(FRDGBuilder& GraphBuilder, const FFolioShaderFilterInputs& Inputs) const override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HeatFilter::AddPasses);

			const float Amplitude = Arg(Inputs.Args, 0, 4.0f) * Inputs.PixelScale;
			const float Ripples = Arg(Inputs.Args, 1, 3.0f);
			const float Speed = Arg(Inputs.Args, 2, 1.0f);

			FFolioHeatPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFolioHeatPS::FParameters>();
			Parameters->SourceTexture = Inputs.Input;
			Parameters->SourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->InvSize = FVector2f(1.0f / Inputs.Size.X, 1.0f / Inputs.Size.Y);
			Parameters->HeatParameters = FVector4f(Amplitude, Ripples, Speed, Inputs.Time);
			Parameters->RenderTargets[0] = FRenderTargetBinding(Inputs.Output, ERenderTargetLoadAction::ENoAction);

			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FFolioHeatPS> PixelShader(ShaderMap);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("FolioHeat"),
				PixelShader, Parameters, FIntRect(FIntPoint::ZeroValue, Inputs.Size));
		}

		virtual float GetReach(const TConstArrayView<FFolioFilterArg> Args) const override
		{
			return FMath::Abs(Arg(Args, 0, 4.0f));
		}

		virtual bool IsAnimated(const TConstArrayView<FFolioFilterArg> Args) const override
		{
			return Arg(Args, 2, 1.0f) != 0.0f;
		}

	private:
		static float Arg(const TConstArrayView<FFolioFilterArg> Args, const int32 Index, const float Default)
		{
			return Args.IsValidIndex(Index) && Args[Index].Kind != FFolioFilterArg::EKind::Colour
				&& Args[Index].Kind != FFolioFilterArg::EKind::Word ? Args[Index].Value : Default;
		}
	};

	FAutoConsoleCommand RegisterExamplesCommand(
		TEXT("folio.Filters.RegisterExamples"),
		TEXT("Registers Folio's example shader filters, `heat` for now, so examples/shader_filters.rml draws in the ")
		TEXT("engine as it does in the preview. A game registers its own with Folio::RegisterShaderFilter."),
		FConsoleCommandDelegate::CreateLambda([]
		{
			Folio::RegisterShaderFilter(TEXT("heat"), MakeShared<FHeatFilter, ESPMode::ThreadSafe>());
			UE_LOG(LogFolio, Display, TEXT("registered the example shader filter 'heat'"));
		}));
}

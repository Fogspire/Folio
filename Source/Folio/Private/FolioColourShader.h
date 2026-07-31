#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class UTextureRenderTarget2D;

namespace Folio
{
	/**
	 * The colour filters, in the order the shader's mode parameter expects.
	 *
	 * These are the ones RCSS names and RmlUi hands over as a compiled filter. Everything about them
	 * happens on one texel, which is why they can be a mode on one shader rather than eight shaders.
	 */
	enum class EColourFilter : uint8
	{
		Brightness,
		Contrast,
		Grayscale,
		Invert,
		Opacity,
		Saturate,
		Sepia,
		HueRotate,
	};

	/** One filter and its argument. Hue-rotate's is in radians; the rest are multipliers. */
	struct FColourFilter
	{
		EColourFilter Kind = EColourFilter::Brightness;
		float Amount = 1.0f;
	};

	/**
	 * Applies filters to a render target in place, in order.
	 *
	 * In order matters: `filter: saturate(0) brightness(2)` and `brightness(2) saturate(0)` are
	 * different pictures, and CSS says the first named runs first.
	 *
	 * Enqueues its own render commands, so it is safe to call from the game thread during a paint
	 * pass - the work lands ahead of Slate's own rendering for the same frame, which is what keeps the
	 * result available to the geometry that samples it.
	 */
	void ApplyColourFilters(UTextureRenderTarget2D* Target, TArrayView<const FColourFilter> Filters);
}

/** One texel in, one texel out. See Shaders/Private/FolioColour.usf for why alpha needs care. */
class FFolioColourPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioColourPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioColourPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER(FVector4f, ColourParameters)
		SHADER_PARAMETER(FVector2f, InvSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

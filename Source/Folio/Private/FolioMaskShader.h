#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class UTextureRenderTarget2D;

namespace Folio
{
	/** How a mask layer's texel becomes coverage. */
	struct FMaskOptions
	{
		/** Alpha, which is what a decorator drawn into a transparent layer produces. */
		bool bUseAlpha = true;

		/** The mask cuts a hole rather than a window. */
		bool bInvert = false;
	};

	/**
	 * Multiplies a layer's alpha by a mask layer's, in place.
	 *
	 * A real alpha mask rather than a stencil: the stencil path records coverage, so a soft-edged mask
	 * comes out hard. Both layers are the same size by construction - RmlUi pushes the mask layer over
	 * the one it masks - so this is one pass with no transform.
	 */
	void ApplyMask(UTextureRenderTarget2D* Target, UTextureRenderTarget2D* Mask, const FMaskOptions& Options);
}

class FFolioMaskPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioMaskPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, MaskSampler)
		SHADER_PARAMETER(FVector4f, MaskParameters)
		SHADER_PARAMETER(FVector2f, InvSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

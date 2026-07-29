#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FRHICommandListImmediate;
class FRHITexture;

/**
 * The blur behind box-shadow and filter: blur().
 *
 * Separable, so a wide radius costs two passes of 2N+1 taps rather than one of (2N+1)^2.
 * That matters because a box-shadow blur radius is routinely 20-40dp.
 */
class FFolioBlurPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioBlurPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioBlurPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER(FVector4f, BlurParameters)
		SHADER_PARAMETER(FVector2f, InvSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

namespace Folio
{
	/**
	 * Blurs a render target in place, through a scratch target of the same size.
	 *
	 * Sigma is in pixels. Enqueues its own render commands, so it is safe to call from the
	 * game thread during a paint pass: the work lands ahead of Slate's own rendering for the
	 * same frame, which is what keeps the result available to the geometry that samples it.
	 */
	void BlurRenderTarget(class UTextureRenderTarget2D* Target, float Sigma);
}

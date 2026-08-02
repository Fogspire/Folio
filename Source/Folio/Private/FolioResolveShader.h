#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class UTextureRenderTarget2D;

namespace Folio
{
	/**
	 * Averages a supersampled target into one the size the screen wants.
	 *
	 * Drawing the big target with a quad and letting the sampler shrink it is not the same thing: the
	 * hardware filter only averages the whole block when the quad's texture coordinates land on texel
	 * corners, which needs the widget's rectangle to be an exact whole number of pixels at exactly the
	 * sample count. Anywhere else it reads one texel of an unfiltered raster, and the result has harder
	 * edges than not supersampling at all.
	 *
	 * Destination must be Source divided by Samples, each way.
	 */
	void ResolveSupersample(UTextureRenderTarget2D* Source, UTextureRenderTarget2D* Destination, int32 Samples);
}

class FFolioResolvePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioResolvePS);
	SHADER_USE_PARAMETER_STRUCT(FFolioResolvePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		SHADER_PARAMETER(FVector4f, ResolveParameters)
		SHADER_PARAMETER(FVector2f, InvSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

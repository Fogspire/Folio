#pragma once

#include "CoreMinimal.h"
#include "FolioUIShader.h"
#include "GlobalShader.h"
#include "RenderGraphResources.h"
#include "ShaderParameterStruct.h"

/**
 * What a world-space document is drawn with, after the tonemapper.
 *
 * Separate from the layer path's parameters because the transform is a full matrix rather than a scale
 * and offset, and because occlusion has to be done in the shader: there is no depth buffer bound at this
 * point in the frame, so scene depth is read as a texture and compared.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FFolioWorldParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, UITexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, UISampler)

	/** Document pixels, top left origin, to clip space. */
	SHADER_PARAMETER(FMatrix44f, DocumentToClip)

	/** xy: the field's range in texture coordinates. z: threshold. w: sharpen. */
	SHADER_PARAMETER(FVector4f, MsdfField)
	/** x: effect mode. y: offset. z: softness. */
	SHADER_PARAMETER(FVector4f, MsdfEffect)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)

	/**
	 * xy: display to render resolution scale, because scene depth is at render resolution while this
	 * target is at display resolution whenever an upscaler runs. z: whether to reject occluded fragments.
	 */
	SHADER_PARAMETER(FVector4f, DepthParams)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FFolioWorldVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioWorldVS);
	SHADER_USE_PARAMETER_STRUCT(FFolioWorldVS, FGlobalShader);
	using FParameters = FFolioWorldParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

class FFolioWorldPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioWorldPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioWorldPS, FGlobalShader);
	using FParameters = FFolioWorldParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

class FFolioWorldMsdfPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioWorldMsdfPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioWorldMsdfPS, FGlobalShader);
	using FParameters = FFolioWorldParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

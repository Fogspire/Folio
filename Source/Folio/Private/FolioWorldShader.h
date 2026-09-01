#pragma once

#include "CoreMinimal.h"
#include "FolioUIShader.h"
#include "GlobalShader.h"
#include "SceneView.h"
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

	/**
	 * x: 1 when the bound texture's alpha is straight and the sample has to be premultiplied here.
	 *
	 * Everything Folio decodes is premultiplied on the way in, and the blend state is premultiplied
	 * to match. A texture the game registers is an engine UTexture that nobody premultiplied, and
	 * added rather than blended its transparent pixels wash whatever is behind the element.
	 */
	SHADER_PARAMETER(FVector4f, UIOptions)

	/** For ScreenPositionScaleBias and BufferSizeAndInvSize, which is how a fragment finds its depth pixel. */
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioSceneDepth)

	/** This target's view rect: xy the origin in pixels, zw one over its size. */
	SHADER_PARAMETER(FVector4f, FolioOutputRect)

	/** 0 draws through geometry, 1 rejects fragments behind it, 2 shows what the test reads. */
	SHADER_PARAMETER(float, FolioOcclusionMode)

	/**
	 * In-scene drawing. xy: one over the target extent. z: remap clip space out of the view rect into the
	 * whole target. w: scale colour by the view's pre-exposure.
	 *
	 * Both corrections exist because the in-scene path draws into a texture the renderer owns, sized and
	 * scaled on the renderer's terms, rather than into a screen pass of Folio's own.
	 */
	SHADER_PARAMETER(FVector4f, FolioSceneTarget)

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

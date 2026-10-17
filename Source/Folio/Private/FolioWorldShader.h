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

	/**
	 * The after-DOF translucency the renderer composited before the tonemapper: colour, whose alpha is how
	 * much of the scene shows through it and whose rgb is what it adds, and the modulate texture that tints
	 * what shows through. A document drawn over them has to sit behind them rather than on top, and alpha
	 * alone cannot do it: additive glow leaves alpha at one. Bound to dummies where there is nothing to fade
	 * by, a black alpha-one colour and a white modulate, which hide nothing and add nothing.
	 */
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioTranslucencyDof)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioTranslucencyDofModulate)

	/**
	 * xy: where the view starts in the after-DOF textures, as a fraction of their extent. zw: the view's size
	 * as a fraction of it. They can be lower resolution than the scene, so pixels would not line up.
	 */
	SHADER_PARAMETER(FVector4f, FolioTranslucencyDofRect)

	/** The same for the after-motion-blur layer, which the renderer composites over the after-DOF one. */
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioTranslucencyMb)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioTranslucencyMbModulate)
	SHADER_PARAMETER(FVector4f, FolioTranslucencyMbRect)

	/**
	 * Scene colour from before either layer was composited into it, which is what they were composited over,
	 * and the tonemapped scene colour the document is drawn over, which is what ended up on screen. Black
	 * where there is nothing to fade by.
	 */
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioSceneColourBefore)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FolioSceneColourAfter)

	/** xy: where the view starts in the tonemapped scene colour, as a fraction of its extent. zw: its size. */
	SHADER_PARAMETER(FVector4f, FolioSceneColourAfterRect)

	/** One bilinear clamp sampler for every lookup above. */
	SHADER_PARAMETER_SAMPLER(SamplerState, FolioTranslucencySampler)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/** Fades the document under the translucency in front of it. See UFolioWorldDocument::bBehindTranslucency. */
class FFolioBehindTranslucencyDim : SHADER_PERMUTATION_BOOL("FOLIO_BEHIND_TRANSLUCENCY");
using FFolioWorldPixelPermutation = TShaderPermutationDomain<FFolioBehindTranslucencyDim>;

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
	using FPermutationDomain = FFolioWorldPixelPermutation;

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
	using FPermutationDomain = FFolioWorldPixelPermutation;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

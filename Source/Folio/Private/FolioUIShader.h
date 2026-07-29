#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

/**
 * The shaders the layer path draws RmlUi geometry with.
 *
 * Slate's own path goes through MakeCustomVerts and needs none of this. A layer does, because
 * clip masks need stencil state and neither MakeCustomVerts nor FCanvas exposes it. The one
 * case that forces the issue is an outer box-shadow, which must not paint inside the element
 * it belongs to: RmlUi expresses that as ClipMaskOperation::SetInverse, and "everything except
 * this shape" cannot be written as a clipping rectangle.
 */

/** Matches the vertex layout the layer path uploads: position, uv, colour. */
struct FFolioUIVertex
{
	FVector2f Position;
	FVector2f UV;
	FLinearColor Color;
};

BEGIN_SHADER_PARAMETER_STRUCT(FFolioUIParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, UITexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, UISampler)
	SHADER_PARAMETER(FVector4f, ClipTransform)
	/** xy: the field's range in texture coordinates. z: threshold. w: sharpen. */
	SHADER_PARAMETER(FVector4f, MsdfField)
	/** x: effect mode. y: offset. z: softness. */
	SHADER_PARAMETER(FVector4f, MsdfEffect)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FFolioUIVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioUIVS);
	SHADER_USE_PARAMETER_STRUCT(FFolioUIVS, FGlobalShader);
	using FParameters = FFolioUIParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

class FFolioUIPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioUIPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioUIPS, FGlobalShader);
	using FParameters = FFolioUIParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

/**
 * Resolves a glyph atlas to coverage, so text keeps working inside a box-shadow or a filter.
 *
 * Slate's path gets this from a material. A layer cannot: it draws with its own pipelines and
 * has no material to bind, so the same maths is compiled here as a global shader from the same
 * .ush include.
 */
class FFolioUIMsdfPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioUIMsdfPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioUIMsdfPS, FGlobalShader);
	using FParameters = FFolioUIParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

/** Writes stencil only. Colour writes are off, so it produces no pixels. */
class FFolioUIMaskPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioUIMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioUIMaskPS, FGlobalShader);
	using FParameters = FFolioUIParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}
};

/** The vertex declaration for FFolioUIVertex, created once. */
class FFolioUIVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};

extern TGlobalResource<FFolioUIVertexDeclaration> GFolioUIVertexDeclaration;

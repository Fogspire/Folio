#pragma once

#include "CoreMinimal.h"
#include "FolioCore/FolioShaderFilters.h"
#include "FolioShaderFilters.h"
#include "GlobalShader.h"
#include "RenderGraphFwd.h"
#include "RenderGraphResources.h"
#include "ShaderParameterStruct.h"

class FSlateWindowElementList;
class UTextureRenderTarget2D;

/**
 * glow and drop-shadow: a blurred, tinted copy of the layer's coverage put behind it.
 *
 * Mode 0 writes the tinted coverage, offset; mode 1 puts the blurred result behind the original.
 */
class FFolioGlowPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFolioGlowPS);
	SHADER_USE_PARAMETER_STRUCT(FFolioGlowPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HaloTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceSampler)
		/** Premultiplied. */
		SHADER_PARAMETER(FVector4f, GlowColour)
		/** xy: offset in UV. z: strength. w: mode. */
		SHADER_PARAMETER(FVector4f, GlowParameters)
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
	/** A scratch texture a pass can ping-pong with: the same size, format and colour space as Texture. */
	FRDGTextureDesc ScratchDescFor(FRDGTextureRef Texture);

	/**
	 * A filter that runs over a resolved layer as passes of its own: glow, drop-shadow, or a game's shader.
	 *
	 * These run after the colour filters and the blur and before the mask, in the order the stylesheet
	 * named them.
	 */
	struct FLayerPass
	{
		enum class EKind : uint8
		{
			/** glow and drop-shadow, which differ only in offset and strength. */
			Halo,
			Shader,
		};

		EKind Kind = EKind::Halo;

		/** Halo: straight alpha. Offset and sigma in document pixels. */
		FLinearColor Colour = FLinearColor::White;
		FVector2f Offset = FVector2f::ZeroVector;
		float Sigma = 0.0f;
		float Strength = 1.0f;

		/** Shader. A null filter is a name nothing registered, which passes the layer through. */
		FName Name;
		TArray<FFolioFilterArg> Args;
		TSharedPtr<IFolioShaderFilter, ESPMode::ThreadSafe> Filter;

		/** Document pixels the result reaches past what the layer drew. */
		float Reach() const;

		/** Whether the result depends on time, which keeps the layer out of the cache. */
		bool IsAnimated() const;

		/** Everything that decides the result, for the layer cache's print. */
		uint32 Hash() const;
	};

	/** Resolved parser arguments as a pass receives them. */
	TArray<FFolioFilterArg> ToFilterArgs(const std::vector<FShaderFilterArg>& Args);

	/** Runs the passes over a render target in place. PixelScale is texels per document pixel. */
	void ApplyLayerPasses(UTextureRenderTarget2D* Target, TConstArrayView<FLayerPass> Passes, float PixelScale);

	/**
	 * backdrop-filter: shader(...). Runs the passes over what Slate has already drawn inside WindowRect.
	 *
	 * A Slate custom element, so it reads the window's target at its place in the draw order and writes
	 * the result back into the same rectangle. Slate holds the element weakly: the caller keeps what this
	 * returns alive until the render thread has drawn it.
	 */
	TSharedPtr<class ICustomSlateElement, ESPMode::ThreadSafe> DrawBackdropPasses(FSlateWindowElementList& ElementList,
		int32 LayerId, const FIntRect& WindowRect, TArray<FLayerPass> Passes, float PixelScale);

	/** Seconds since the application started, as a pass's Time sees it. */
	float LayerPassTime();
}

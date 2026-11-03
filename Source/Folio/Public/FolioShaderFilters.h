#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FRDGTexture;

/** One argument of `filter: shader("name ...")`, as the pass receives it. */
struct FFolioFilterArg
{
	enum class EKind : uint8
	{
		Number,
		/** A length, in the document's pixels. Multiply by FFolioShaderFilterInputs::PixelScale for texels. */
		Length,
		Colour,
		Word,
	};

	EKind Kind = EKind::Number;
	float Value = 0.0f;

	/** Straight alpha, as written in the stylesheet. */
	FLinearColor Colour = FLinearColor::Transparent;
	FName Word;
};

/** What a pass is handed on the render thread. */
struct FFolioShaderFilterInputs
{
	/**
	 * The element's pixels, premultiplied, in the layer's format.
	 *
	 * For `backdrop-filter` it is what is on screen behind the element instead.
	 */
	FRDGTexture* Input = nullptr;

	/** Where the result goes: same size and format as Input. Every texel must be written. */
	FRDGTexture* Output = nullptr;

	FIntPoint Size = FIntPoint::ZeroValue;

	/** Texels per document pixel, which is above one when the document is supersampled. */
	float PixelScale = 1.0f;

	/** Seconds since the application started, for a pass that animates. */
	float Time = 0.0f;

	/** Set when the pass is running as a backdrop-filter. */
	bool bBackdrop = false;

	TArray<FFolioFilterArg> Args;
};

/**
 * A GPU pass a document reaches with `filter: shader("name args...")` or `backdrop-filter: shader(...)`.
 *
 * Registered once by the game, by name. Folio draws the element into a layer, hands the pass that layer
 * and an output of the same size, and composites the output where the element was. The result is cached
 * with the layer, so a pass over a still element runs once rather than every frame, unless IsAnimated says
 * otherwise.
 *
 * Passes run on the render thread, inside Folio's own graph: add passes to the builder and return.
 */
class IFolioShaderFilter
{
public:
	virtual ~IFolioShaderFilter() = default;

	/** Records the passes that turn Input into Output. Render thread. */
	virtual void AddPasses(FRDGBuilder& GraphBuilder, const FFolioShaderFilterInputs& Inputs) const = 0;

	/**
	 * How far the result reaches past the element's box, in document pixels. Game thread.
	 *
	 * The layer grows by this much. The default is three times the largest length argument.
	 */
	virtual float GetReach(TConstArrayView<FFolioFilterArg> Args) const;

	/**
	 * Whether the result changes with time rather than only with the element.
	 *
	 * An animated pass is never cached, and keeps its document drawing every frame while it is on screen.
	 */
	virtual bool IsAnimated(TConstArrayView<FFolioFilterArg> Args) const { return false; }
};

namespace Folio
{
	/**
	 * Makes a pass available to every document as `shader("<Name> ...")`.
	 *
	 * Registering a name that is already taken replaces it. Documents already showing the filter pick the
	 * new pass up the next time the element redraws.
	 */
	FOLIO_API void RegisterShaderFilter(FName Name, TSharedRef<IFolioShaderFilter, ESPMode::ThreadSafe> Filter);
	FOLIO_API void UnregisterShaderFilter(FName Name);
	FOLIO_API TSharedPtr<IFolioShaderFilter, ESPMode::ThreadSafe> FindShaderFilter(FName Name);
}

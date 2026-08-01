#pragma once

#include "FolioColourShader.h"

#include "CoreMinimal.h"
#include "FolioDrawList.h"
#include "FolioRmlUi.h"
#include "FolioUIShader.h"
#include "Rendering/RenderingCommon.h"
#include "UObject/StrongObjectPtr.h"

class UTexture;
class UTextureRenderTarget2D;

/**
 * RmlUi's render layers, as render targets with a stencil buffer.
 *
 * A layer is an offscreen surface that geometry is drawn into and then read back: it is what
 * box-shadow, filter, backdrop-filter and mask-image are all built on. Slate has nothing like
 * it reachable through MakeCustomVerts, so the layer path leaves Slate and draws with RHI.
 *
 * It draws with RHI rather than FCanvas because of clip masks. An outer box-shadow must not
 * paint inside the element it belongs to, which RmlUi expresses as ClipMaskOperation
 * ::SetInverse - "everywhere except this shape". A clipping rectangle cannot say that, and
 * FCanvas exposes no stencil state. Without it the shadow paints over its own element's
 * interior and darkens it by exactly the shadow's own alpha, which is the bug this replaced.
 *
 * Draws are recorded on the game thread as RmlUi issues them, then replayed on the render
 * thread in one pass per layer. Recording is what lets the stencil live for a whole layer: a
 * mask is set once and every later draw tests against it, which cannot work if each draw is
 * its own transient graph.
 */
class FFolioLayerStack
{
public:
	FFolioLayerStack();
	~FFolioLayerStack();

	bool Push(FIntPoint Size);

	/**
	 * Pushes a layer matching the current one's geometry.
	 *
	 * Every layer in a stack shares one space: geometry is recorded against the bottom layer's origin,
	 * and compositing draws a quad over the whole target. So a nested layer sized differently is not a
	 * smaller view of the same picture, it is a different picture - and mask-image is where that shows,
	 * since the mask and what it masks are sampled at the same coordinate.
	 */
	bool PushMatchingTop();

	/**
	 * Pushes a layer that resolves into a target the caller owns, rather than one from the pool.
	 *
	 * What a render-target surface needs: the destination outlives the frame and is sampled by a material,
	 * so it must not go back to the pool to be handed to the next layer and cleared.
	 */
	bool PushInto(class UTextureRenderTarget2D* Target, FIntPoint ClipSize = FIntPoint::ZeroValue);

	/** Resolves the layer and returns its target to the pool. */
	void Pop();

	bool IsEmpty() const { return Stack.IsEmpty(); }
	int32 Num() const { return Stack.Num(); }

	/** Records a draw the caller has already assembled, for a path the helpers below do not fit. */
	void AddDraw(FFolioDraw&& Draw);

	/** Records a triangle list. Positions are in layer space. */
	void DrawTriangles(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		UTexture* Texture, FVector2f Offset);

	/**
	 * Records text, whose texture is a distance field rather than an image.
	 *
	 * Field and Effect are the shader's MsdfField and MsdfEffect, packed by the caller so the
	 * layer stack does not have to know what a glyph atlas is.
	 */
	void DrawField(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		UTexture* Texture, FVector2f Offset, FVector4f Field, FVector4f Effect);

	/** Records a clip mask operation, which every later draw in this layer tests against. */
	void SetClipMask(Rml::ClipMaskOperation Operation, const TArray<FSlateVertex>& Vertices,
		const TArray<SlateIndex>& Indices, FVector2f Offset);

	void EnableClipMask(bool bEnable);

	/** The recording the top layer is filling, so a scissor can be set on it. Null when empty. */
	FFolioDrawList* CurrentDrawList();

	/** Resolves the current layer and composites it into the one below. */
	void CompositeDown(bool bBlend, float BlurSigma,
		TArrayView<const Folio::FColourFilter> Colour = {}, UTextureRenderTarget2D* Mask = nullptr);

	/**
	 * Resolves the current layer, hands its target to the caller and replaces it.
	 *
	 * SaveLayerAsTexture needs this: RmlUi keeps the result as a texture with its own
	 * lifetime, so the target cannot return to the pool while the layer is still in use.
	 *
	 * So does compositing the bottom layer, whose destination is Slate rather than another
	 * target: Slate reads the texture at the end of the frame, long after the pool would have
	 * handed it to the next layer and cleared it.
	 */
	UTextureRenderTarget2D* DetachCurrent(float BlurSigma = 0.0f,
		TArrayView<const Folio::FColourFilter> Colour = {}, UTextureRenderTarget2D* Mask = nullptr);

	void Reset();

private:
	struct FLayer
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		FIntPoint Size = FIntPoint::ZeroValue;

		/**
		 * The space the recorded geometry is in, when that is not the target's own size.
		 *
		 * A surface renders a document into a target larger than the document, so the replay scales rather
		 * than the layout. Unset means the two are the same, which is every other layer.
		 */
		FIntPoint ClipSize = FIntPoint::ZeroValue;
		FFolioDrawList Draws;

		bool bClipMaskEnabled = false;
		bool bClipMaskInverted = false;

		/** Set for a target the caller owns, which Pop resolves but does not pool. */
		bool bCallerOwned = false;

		/**
		 * Set once this layer's target is being sampled by a composite quad in the layer below.
		 *
		 * Its lifetime then belongs to that layer, not to this one: popping is what RmlUi does after a
		 * composite, and returning the target to the pool there hands it straight back to the next
		 * sibling, which clears it. The pending quad is left sampling whatever that sibling drew.
		 */
		bool bHandedOff = false;

		/**
		 * Targets this layer's own draws sample, released once it has resolved.
		 *
		 * Every layer composited into this one is in here. Holding them is what makes a document with
		 * more than one filtered element work at all - without it only the last one is correct, and it
		 * looks like the others were never drawn.
		 */
		TArray<TStrongObjectPtr<UTextureRenderTarget2D>> Sampled;
	};

	UTextureRenderTarget2D* Acquire(FIntPoint Size);
	void Release(UTextureRenderTarget2D* Target);

	/** Replays a layer's recorded draws into its target, then clears the recording. */
	void Resolve(FLayer& Layer);

	/** Resolves, then returns every target the layer sampled to the pool. */
	void ResolveAndRecycle(FLayer& Layer);

	TArray<FLayer> Stack;
	TMultiMap<FIntPoint, TStrongObjectPtr<UTextureRenderTarget2D>> Pool;
};

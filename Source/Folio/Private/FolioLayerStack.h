#pragma once

#include "CoreMinimal.h"
#include "Rendering/RenderingCommon.h"
#include "UObject/StrongObjectPtr.h"

class FCanvas;
class UTextureRenderTarget2D;

/**
 * RmlUi's render layers, as render targets.
 *
 * A layer is an offscreen surface that geometry is drawn into and then read back: it is what
 * box-shadow, filter, backdrop-filter and mask-image are all built on. Slate has nothing like
 * it reachable through MakeCustomVerts, so the layer path leaves Slate entirely and draws
 * into a render target, and the result comes back as an ordinary texture that Slate can then
 * sample like any other.
 *
 * All of this happens on the game thread during the widget's paint pass. FCanvas defers the
 * actual RHI work, and the commands it enqueues land ahead of Slate's own rendering for the
 * same frame, so a target is filled before the geometry that samples it is drawn.
 *
 * Targets are pooled by size. A document's layers are the same handful of sizes frame after
 * frame, and allocating a render target per shadow per frame would be the slowest thing in
 * the plugin.
 */
class FFolioLayerStack
{
public:
	FFolioLayerStack();
	~FFolioLayerStack();

	/** Pushes a new layer of the given size and makes it current. Returns false if it failed. */
	bool Push(FIntPoint Size);

	/** Ends the current layer. Its target returns to the pool once nothing references it. */
	void Pop();

	bool IsEmpty() const { return Stack.IsEmpty(); }
	int32 Num() const { return Stack.Num(); }

	UTextureRenderTarget2D* GetCurrent() const;

	/** The layer below the current one, which is what CompositeLayers blends into. */
	UTextureRenderTarget2D* GetBelow() const;

	/**
	 * Hands the current layer's target to the caller and replaces it with a fresh one.
	 *
	 * This is what SaveLayerAsTexture needs: RmlUi keeps the result as a texture with its own
	 * lifetime, so the target cannot go back to the pool while the layer is still in use.
	 */
	UTextureRenderTarget2D* DetachCurrent();

	/** Draws one triangle list into the current layer. Positions are in layer space. */
	void DrawTriangles(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		class UTexture* Texture, FVector2f Offset);

	/**
	 * Draws the current layer into the one below it, without disturbing the stack.
	 *
	 * Popping and re-pushing to reach the destination looks equivalent and is not: a pushed
	 * layer is cleared, so the source's contents are lost and the panel ends up composited
	 * from a blank target.
	 */
	void CompositeDown(bool bBlend);

	/** Releases every pooled target. Called when the renderer shuts down. */
	void Reset();

private:
	struct FLayer
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		FIntPoint Size = FIntPoint::ZeroValue;
	};

	UTextureRenderTarget2D* Acquire(FIntPoint Size, bool bClear);
	void Release(UTextureRenderTarget2D* Target);

	/** Opens a canvas onto the current layer, or returns null if there is no layer. */
	FCanvas* BeginCanvas();
	void EndCanvas();

	TArray<FLayer> Stack;
	TMultiMap<FIntPoint, TStrongObjectPtr<UTextureRenderTarget2D>> Pool;

	FCanvas* Canvas = nullptr;
	UTextureRenderTarget2D* CanvasTarget = nullptr;
};

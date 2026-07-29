#pragma once

#include "CoreMinimal.h"
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

	/** Resolves the layer and returns its target to the pool. */
	void Pop();

	bool IsEmpty() const { return Stack.IsEmpty(); }
	int32 Num() const { return Stack.Num(); }

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

	/** Resolves the current layer and composites it into the one below. */
	void CompositeDown(bool bBlend, float BlurSigma);

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
	UTextureRenderTarget2D* DetachCurrent(float BlurSigma = 0.0f);

	void Reset();

private:
	/** One recorded draw. Vertex data is copied: RmlUi may release its own at any point. */
	struct FDraw
	{
		enum class EKind : uint8 { Geometry, Field, MaskSet, MaskSetInverse, MaskIntersect };

		EKind Kind = EKind::Geometry;
		TArray<FFolioUIVertex> Vertices;
		TArray<uint32> Indices;
		TStrongObjectPtr<UTexture> Texture;

		/** Whether this geometry tests the stencil, and on which side. */
		bool bMasked = false;
		bool bMaskInverted = false;

		FVector4f Field = FVector4f::Zero();
		FVector4f Effect = FVector4f::Zero();
	};

	struct FLayer
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		FIntPoint Size = FIntPoint::ZeroValue;
		TArray<FDraw> Draws;

		bool bClipMaskEnabled = false;
		bool bClipMaskInverted = false;
	};

	UTextureRenderTarget2D* Acquire(FIntPoint Size);
	void Release(UTextureRenderTarget2D* Target);

	/** Replays a layer's recorded draws into its target, then clears the recording. */
	void Resolve(FLayer& Layer);

	void Record(FDraw&& Draw);

	static void Convert(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		FVector2f Offset, bool bUnpremultiply, FDraw& OutDraw);

	TArray<FLayer> Stack;
	TMultiMap<FIntPoint, TStrongObjectPtr<UTextureRenderTarget2D>> Pool;
};

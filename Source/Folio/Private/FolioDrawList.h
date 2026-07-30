#pragma once

#include "CoreMinimal.h"
#include "FolioRmlUi.h"
#include "FolioUIShader.h"
#include "Rendering/RenderingCommon.h"
#include "UObject/StrongObjectPtr.h"

class UTexture;

/**
 * One recorded draw, in the neutral form both replays read.
 *
 * Vertex data is copied rather than referenced: RmlUi may release its own compiled geometry at any
 * point, and a recording outlives the call that made it by definition.
 */
struct FFolioDraw
{
	enum class EKind : uint8 { Geometry, Field, MaskSet, MaskSetInverse, MaskIntersect };

	EKind Kind = EKind::Geometry;
	TArray<FFolioUIVertex> Vertices;
	TArray<uint32> Indices;
	TStrongObjectPtr<UTexture> Texture;

	/** Whether this geometry tests the stencil, and on which side. */
	bool bMasked = false;
	bool bMaskInverted = false;

	/** The shader's MsdfField and MsdfEffect, packed by whoever recorded it. */
	FVector4f Field = FVector4f::Zero();
	FVector4f Effect = FVector4f::Zero();
};

/**
 * A document's draw calls, recorded on the game thread for something else to replay.
 *
 * Extracted from the layer stack because two things now need it. A render layer replays into an
 * offscreen target with an orthographic transform; a world-space document replays into the
 * post-tonemap target with a world-view-projection one. The recording is identical either way - only
 * the transform and the destination differ - so it lives here rather than in either of them.
 *
 * Recording rather than drawing immediately is also what lets a stencil live across many draws: a
 * clip mask is set once and every later draw tests against it, which cannot work if each draw is its
 * own transient graph.
 */
class FFolioDrawList
{
public:
	/** Records a triangle list. Positions are in the recording's own 2D space. */
	void AddTriangles(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		UTexture* Texture, FVector2f Offset, bool bMasked, bool bMaskInverted);

	/**
	 * Records text, whose texture is a distance field rather than an image.
	 *
	 * Unpremultiplied on the way in, because decoding sRGB on a premultiplied colour raises the
	 * product to a power rather than the colour and leaves a dark rim around every glyph.
	 */
	void AddField(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		UTexture* Texture, FVector2f Offset, FVector4f Field, FVector4f Effect,
		bool bMasked, bool bMaskInverted);

	/** Records a clip mask operation, which every later draw tests against. */
	void AddClipMask(Rml::ClipMaskOperation Operation, const TArray<FSlateVertex>& Vertices,
		const TArray<SlateIndex>& Indices, FVector2f Offset);

	void Reset() { Draws.Reset(); }
	bool IsEmpty() const { return Draws.IsEmpty(); }
	int32 Num() const { return Draws.Num(); }

	const TArray<FFolioDraw>& GetDraws() const { return Draws; }
	TArray<FFolioDraw>& GetDraws() { return Draws; }

	/** Total triangles across every recorded draw, for reporting rather than for drawing. */
	int32 CountTriangles() const;

	/** Slate's vertex layout into the shader's, with the offset folded in. */
	static void Convert(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
		FVector2f Offset, bool bUnpremultiply, FFolioDraw& OutDraw);

private:
	TArray<FFolioDraw> Draws;
};

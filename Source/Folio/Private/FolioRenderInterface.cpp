#include "FolioRenderInterface.h"

#include "FolioInsights.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FolioBlurShader.h"
#include "FolioCore/MsdfSidecar.h"
#include "FolioCore/FolioShaderEffects.h"
#include "FolioFileInterface.h"
#include "FolioModule.h"
#include "FolioTextures.h"
#include "HAL/PlatformFileManager.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/SlateResourceHandle.h"
#include "TextureResource.h"
#include "Textures/SlateShaderResource.h"

namespace
{
	FColor ToSlateColor(const Rml::ColourbPremultiplied& Colour)
	{
		// Both sides are premultiplied, so this is a channel swap and nothing else.
		return FColor(Colour.red, Colour.green, Colour.blue, Colour.alpha);
	}

	FVector2f ToVector2f(const Rml::Vector2f& Value)
	{
		return FVector2f(Value.x, Value.y);
	}
}

FFolioRenderInterface::FFolioRenderInterface() = default;

const FSlateResourceHandle& FFolioRenderInterface::GetWhiteHandle()
{
	// Built on demand rather than in the constructor: the module starts before Slate's
	// renderer exists, and a handle taken then is permanently invalid.
	if (WhiteHandle.IsValid())
	{
		return WhiteHandle;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return WhiteHandle;
	}

	if (!WhiteBrush.IsValid())
	{
		const uint8 Opaque[4] = { 255, 255, 255, 255 };
		WhiteBrush = MakeDynamicBrush(TEXT("Folio.White"), Opaque, FIntPoint(1, 1));
		if (!WhiteBrush.IsValid())
		{
			return WhiteHandle;
		}
	}

	WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
	return WhiteHandle;
}

FFolioRenderInterface::~FFolioRenderInterface()
{
	Geometries.Empty();
	Textures.Empty();
}

void FFolioRenderInterface::BeginFrame(FSlateWindowElementList& InElementList, const FGeometry& InGeometry,
	const int32 InLayerId, const FIntPoint InContextSize, const float InAlpha)
{
	FrameAlpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);
	ElementList = &InElementList;
	Geometry = InGeometry;
	LayerId = InLayerId;
	ContextSize = InContextSize;
	MaxLayerId = InLayerId;

	bScissorEnabled = false;
	bClipPushed = false;
	bHasTransform = false;
	ClipMaskRect.Reset();
	LayerDepth = 0;

	// Only the bottom layer sets this, and a surface never has one - it pushes into a target the
	// caller owns. Left alone it keeps the offset of whatever screen document ran last, and every
	// layer on the surface is shifted by it.
	LayerOrigin = FIntPoint::ZeroValue;

	RecordedLayerMarks.Reset();
	RmlTransform = FSlateRenderTransform();
	PendingBackdropSigma = 0.0f;

	// Released now rather than at EndFrame: Slate reads them after the last draw is issued.
	LayerComposites.Reset();
}

void FFolioRenderInterface::BeginRecording(FFolioDrawList& InDrawList, const FIntPoint InContextSize)
{
	DrawList = &InDrawList;
	DrawList->Reset();
	DeferredFields.Reset();
	bDeferFields = false;
	FrameAlpha = 1.0f;

	ElementList = nullptr;
	ContextSize = InContextSize;
	LayerId = 0;
	MaxLayerId = 0;

	// The recording has no geometry of its own: positions stay in document space and whoever replays it
	// decides where that lands. An identity geometry keeps the shared paths from having to care.
	Geometry = FGeometry::MakeRoot(FVector2D(InContextSize.X, InContextSize.Y), FSlateLayoutTransform());

	bScissorEnabled = false;
	bClipPushed = false;
	bHasTransform = false;
	ClipMaskRect.Reset();
	LayerDepth = 0;

	// Only the bottom layer sets this, and a surface never has one - it pushes into a target the
	// caller owns. Left alone it keeps the offset of whatever screen document ran last, and every
	// layer on the surface is shifted by it.
	LayerOrigin = FIntPoint::ZeroValue;

	RecordedLayerMarks.Reset();
	RmlTransform = FSlateRenderTransform();
	PendingBackdropSigma = 0.0f;
	LayerComposites.Reset();
}

bool FFolioRenderInterface::BeginSurface(const FIntPoint InContextSize, UTextureRenderTarget2D* Target,
	const int32 InSupersample, const bool bInDeferFields)
{
	ElementList = nullptr;
	DrawList = nullptr;
	DeferredFields.Reset();
	bDeferFields = bInDeferFields;
	ContextSize = InContextSize;
	LayerId = 0;
	MaxLayerId = 0;

	// Positions stay in document space, as they do for a recording: the base layer is the size of the
	// document, so there is nothing to transform them by.
	Geometry = FGeometry::MakeRoot(FVector2D(InContextSize.X, InContextSize.Y), FSlateLayoutTransform());

	bScissorEnabled = false;
	bClipPushed = false;
	bHasTransform = false;
	ClipMaskRect.Reset();
	LayerDepth = 0;

	// Only the bottom layer sets this, and a surface never has one - it pushes into a target the
	// caller owns. Left alone it keeps the offset of whatever screen document ran last, and every
	// layer on the surface is shifted by it.
	LayerOrigin = FIntPoint::ZeroValue;

	RecordedLayerMarks.Reset();
	RmlTransform = FSlateRenderTransform();
	PendingBackdropSigma = 0.0f;
	LayerComposites.Reset();

	// Every layer the document pushes is allocated this much denser than the space it is recorded in,
	// so the raster is finer and the layout is untouched.
	Layers.SetResolutionScale(InSupersample);

	// The document's own layer, standing in for Slate. Every effect layer RmlUi pushes composites down
	// into this one, exactly as it would composite into the screen. The caller's target rather than a
	// pooled one, because a material samples it long after the pool would have reused it.
	//
	// The context size is the clip: the target is larger, and the replay scales the document onto it.
	bSurface = Layers.PushInto(Target, InContextSize);
	return bSurface;
}

void FFolioRenderInterface::EndSurface()
{
	if (!bSurface)
	{
		return;
	}

	Layers.Pop();
	Layers.SetResolutionScale(1);
	bSurface = false;
	bDeferFields = false;

	EndFrame();
}

void FFolioRenderInterface::EndFrame()
{
	// A document that leaves clipping enabled would otherwise leak its clip zone into
	// whatever Slate draws after it.
	if (bClipPushed && ElementList)
	{
		ElementList->PopClip();
		bClipPushed = false;
	}

	// A document that fails mid-pass can leave a layer open, which would otherwise send the
	// next frame's geometry into an orphaned render target instead of to the screen.
	while (!Layers.IsEmpty())
	{
		Layers.Pop();
	}
	LayerDepth = 0;
	RecordedLayerMarks.Reset();

	ElementList = nullptr;
	DrawList = nullptr;
}

//
// Geometry
//

Rml::CompiledGeometryHandle FFolioRenderInterface::CompileGeometry(const Rml::Span<const Rml::Vertex> Vertices,
	const Rml::Span<const int> Indices)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_CompileGeometry);
	SCOPE_CYCLE_COUNTER(STAT_FolioCompileGeometry);

	TUniquePtr<FCompiledGeometry> Compiled = MakeUnique<FCompiledGeometry>();

	// RmlUi expresses `image(src repeat)` as one quad whose coordinates run to the tile count and
	// leaves the wrapping to the sampler, and it never tells the backend which decorator it is
	// drawing for. Sniffing the coordinates is the only signal there is. The tolerance is loose
	// enough that a quad ending a fraction of a texel past its edge is not read as a tile.
	constexpr float UnitSquareTolerance = 1.0e-3f;

	Compiled->Vertices.Reserve(Vertices.size());
	for (const Rml::Vertex& Vertex : Vertices)
	{
		FSlateVertex& Out = Compiled->Vertices.AddDefaulted_GetRef();
		Out.Position = ToVector2f(Vertex.position);
		Out.TexCoords[0] = Vertex.tex_coord.x;
		Out.TexCoords[1] = Vertex.tex_coord.y;
		Out.TexCoords[2] = 1.0f;
		Out.TexCoords[3] = 1.0f;
		Out.MaterialTexCoords = FVector2f(Vertex.tex_coord.x, Vertex.tex_coord.y);
		Out.Color = ToSlateColor(Vertex.colour);

		Compiled->bTiles |= Vertex.tex_coord.x < -UnitSquareTolerance
			|| Vertex.tex_coord.x > 1.0f + UnitSquareTolerance
			|| Vertex.tex_coord.y < -UnitSquareTolerance
			|| Vertex.tex_coord.y > 1.0f + UnitSquareTolerance;
	}

	Compiled->Indices.Reserve(Indices.size());
	for (const int Index : Indices)
	{
		Compiled->Indices.Add(static_cast<SlateIndex>(Index));
	}

	const Rml::CompiledGeometryHandle Handle = NextHandle++;
	Geometries.Add(Handle, MoveTemp(Compiled));
	return Handle;
}

const TArray<FSlateVertex>& FFolioRenderInterface::PositionedVertices(const FCompiledGeometry& Compiled,
	const Rml::Vector2f Translation, FVector2f& OutOffset)
{
	if (!bHasTransform)
	{
		OutOffset = FVector2f(Translation.x, Translation.y);
		return Compiled.Vertices;
	}

	// Baked into the positions, because neither a recording nor a layer replay carries a transform: both
	// draw in document space with a plain offset. Without this a `transform` in RCSS is dropped in silence
	// everywhere except the Slate path, so it animates on screen and does nothing on a surface.
	ScratchVertices.Reset(Compiled.Vertices.Num());

	const FVector2f Offset(Translation.x, Translation.y);
	for (const FSlateVertex& Source : Compiled.Vertices)
	{
		FSlateVertex& Out = ScratchVertices.Add_GetRef(Source);
		Out.Position = FVector2f(TransformPoint(RmlTransform, FVector2f(Source.Position) + Offset));
	}

	OutOffset = FVector2f::ZeroVector;
	return ScratchVertices;
}

void FFolioRenderInterface::RenderGeometry(const Rml::CompiledGeometryHandle Handle,
	const Rml::Vector2f Translation, const Rml::TextureHandle Texture)
{
	// A surface has neither an element list nor a recording: its destination is the bottom layer, which the
	// layer branches below already write to.
	if (!ElementList && !DrawList && Layers.IsEmpty())
	{
		return;
	}

	const TUniquePtr<FCompiledGeometry>* Found = Geometries.Find(Handle);
	if (!Found || !Found->IsValid())
	{
		return;
	}

	const FCompiledGeometry& Compiled = **Found;
	if (Compiled.Vertices.IsEmpty() || Compiled.Indices.IsEmpty())
	{
		return;
	}

	// A field page resolves through a shader rather than as an image: through the material on
	// Slate's path, and through FFolioUIMsdfPS on the layer and recording paths, all compiled from
	// FolioMsdf.ush.
	if (FMsdfBinding* Field = FindMsdfBinding(Texture))
	{
		if (DrawList)
		{
			FVector4f FieldParams, EffectParams;
			PackMsdfParameters(*Field, FieldParams, EffectParams);

			// Unmasked: a recording has no stencil, because a clip mask is a layer's mechanism and a
			// recording has no layer. Clipping a world-space document is the mesh's job.
			FVector2f Offset;
			const TArray<FSlateVertex>& Vertices = PositionedVertices(Compiled, Translation, Offset);

			DrawList->AddField(Vertices, Compiled.Indices, Field->Page->Texture.Get(),
				Offset, FieldParams, EffectParams,
				/*bMasked*/ false, /*bMaskInverted*/ false);
			return;
		}

		if (!Layers.IsEmpty())
		{
			FVector2f Offset;
			const TArray<FSlateVertex>& Vertices = PositionedVertices(Compiled, Translation, Offset);

			// Held back for the screen rather than drawn into a target that is about to be scaled down.
			// The offset is folded in, because a run that is put back later is put back on its own.
			if (ShouldDeferField())
			{
				FDeferredField& Deferred = DeferredFields.AddDefaulted_GetRef();
				Deferred.Run.Vertices.Reserve(Vertices.Num());

				for (const FSlateVertex& Source : Vertices)
				{
					FSlateVertex& Out = Deferred.Run.Vertices.Add_GetRef(Source);
					Out.Position = FVector2f(Source.Position) + Offset;
				}

				Deferred.Run.Indices = Compiled.Indices;
				Deferred.Page = Texture;
				Deferred.Bounds = BoundsOf(Vertices, Offset);
				Deferred.Scissor = FIntRect(ScissorRegion.Left(), ScissorRegion.Top(),
					ScissorRegion.Right(), ScissorRegion.Bottom());
				Deferred.bScissored = bScissorEnabled;
				return;
			}

			FVector4f FieldParams, EffectParams;
			PackMsdfParameters(*Field, FieldParams, EffectParams);

			FlushCoveredFields(BoundsOf(Vertices, Offset));

			Layers.DrawField(Vertices, Compiled.Indices, Field->Page->Texture.Get(),
				Offset - LayerOrigin, FieldParams, EffectParams);
			return;
		}

		// Everything else about the Slate draw is unchanged: this is a resource handle swap, not
		// a second path.
		if (const FMsdfBinding* Resolved = ResolveMsdfBinding(Texture))
		{
			ApplyClipping();
			DrawGeometry(Compiled, Translation, Resolved->Handle, FVector2f(1.0f, 1.0f), /*bStraightAlpha*/ true);
		}

		return;
	}

	const FTexture* Bound = FindTexture(Texture);

	if (DrawList)
	{
		UTexture* Recorded = Bound && Bound->Texture.IsValid()
			? static_cast<UTexture*>(Bound->Texture.Get())
			: GetWhiteTexture();

		FVector2f Offset;
		const TArray<FSlateVertex>& Vertices = PositionedVertices(Compiled, Translation, Offset);

		DrawList->AddTriangles(Vertices, Compiled.Indices, Recorded,
			Offset, /*bMasked*/ false, /*bMaskInverted*/ false, Compiled.bTiles,
			Bound && Bound->bStraightAlpha);
		return;
	}

	// While a layer is open every draw belongs to it, not to Slate: the layer is what
	// box-shadow and the filters read back, and geometry that leaked past it would appear
	// twice, once inside the effect and once beside it.
	if (!Layers.IsEmpty())
	{
		UTexture* LayerTexture = Bound && Bound->Texture.IsValid()
			? static_cast<UTexture*>(Bound->Texture.Get())
			: GetWhiteTexture();

		FVector2f Offset;
		const TArray<FSlateVertex>& Vertices = PositionedVertices(Compiled, Translation, Offset);

		FlushCoveredFields(BoundsOf(Vertices, Offset));

		Layers.DrawTriangles(Vertices, Compiled.Indices, LayerTexture, Offset - LayerOrigin,
			Compiled.bTiles, Bound && Bound->bStraightAlpha);
		return;
	}

	const FSlateResourceHandle& Resource = Bound ? Bound->Handle : GetWhiteHandle();

	ApplyClipping();
	DrawGeometry(Compiled, Translation, Resource, FVector2f(1.0f, 1.0f), /*bStraightAlpha*/ false,
		Compiled.bTiles, Bound && Bound->bStraightAlpha);
}

void FFolioRenderInterface::DrawGeometry(const FCompiledGeometry& Compiled, const Rml::Vector2f Translation,
	const FSlateResourceHandle& Resource, const FVector2f UvScale, const bool bStraightAlpha,
	const bool bTiles, const bool bTextureStraightAlpha)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_DrawGeometry);
	SCOPE_CYCLE_COUNTER(STAT_FolioDrawGeometry);
	INC_DWORD_STAT(STAT_FolioDraws);

	// Slate dereferences the resource handle's proxy without checking it, so an invalid one
	// is a crash rather than a missing texture. Untextured geometry is most of a document -
	// every background and border arrives with none - so this is not an edge case.
	if (!ElementList || !Resource.IsValid())
	{
		return;
	}

	// Slate wants window space, RmlUi works in document space, and the two are separated by
	// the widget's own geometry plus whatever transform RCSS has applied. Composing them here
	// rather than baking them at compile time is what lets one compiled geometry be drawn at
	// several positions, which is exactly what RmlUi does with a translated draw.
	FSlateRenderTransform Transform = Geometry.GetAccumulatedRenderTransform();
	if (bHasTransform)
	{
		Transform = Concatenate(RmlTransform, Transform);
	}
	Transform = Concatenate(FSlateRenderTransform(FVector2f(Translation.x, Translation.y)), Transform);

	ScratchVertices.Reset(Compiled.Vertices.Num());
	for (const FSlateVertex& Source : Compiled.Vertices)
	{
		FSlateVertex& Out = ScratchVertices.Add_GetRef(Source);
		Out.Position = FVector2f(TransformPoint(Transform, FVector2f(Source.Position)));

		// Every channel, because RmlUi composites premultiplied: scaling the alpha alone leaves the
		// colour at full strength, and a document would brighten as it faded rather than going away.
		if (FrameAlpha < 1.0f - UE_SMALL_NUMBER)
		{
			Out.Color.R = uint8(float(Out.Color.R) * FrameAlpha);
			Out.Color.G = uint8(float(Out.Color.G) * FrameAlpha);
			Out.Color.B = uint8(float(Out.Color.B) * FrameAlpha);
			Out.Color.A = uint8(float(Out.Color.A) * FrameAlpha);
		}

		// A shader's geometry carries pixel offsets inside the element box rather than UVs,
		// so the gradient path normalises them against the box it baked the texture for.
		Out.TexCoords[0] = Source.TexCoords[0] * UvScale.X;
		Out.TexCoords[1] = Source.TexCoords[1] * UvScale.Y;
		Out.MaterialTexCoords = FVector2f(Out.TexCoords[0], Out.TexCoords[1]);

		// A material batch has its vertex colour decoded from sRGB by Slate's vertex shader,
		// and only the colour: the alpha is left alone. Decoding a premultiplied colour raises
		// the product to a power rather than the straight colour, so the two stop agreeing and
		// every partially covered pixel comes out too dark - a dark rim around every glyph. The
		// colour is handed over straight instead and the material multiplies it back.
		if ((bStraightAlpha || bTextureStraightAlpha) && Out.Color.A != 0 && Out.Color.A != 255)
		{
			const float Alpha = float(Out.Color.A) / 255.0f;
			Out.Color.R = uint8(FMath::Clamp(FMath::RoundToInt(float(Out.Color.R) / Alpha), 0, 255));
			Out.Color.G = uint8(FMath::Clamp(FMath::RoundToInt(float(Out.Color.G) / Alpha), 0, 255));
			Out.Color.B = uint8(FMath::Clamp(FMath::RoundToInt(float(Out.Color.B) / Alpha), 0, 255));
		}
	}

	// Wrap sampling, which `image(src repeat)` is the only thing to want. The batch flags are the
	// only route to it: Slate chooses the sampler per batch and never consults the texture's own
	// address mode, so `TA_Clamp` on the UTexture is not what decides this.
	//
	// An atlased proxy stays clamped. Wrapping a sub-rectangle samples its neighbours in the sheet
	// rather than itself, which is why RmlUi refuses `repeat` on a sprite in the first place.
	ESlateBatchDrawFlag BatchFlags = ESlateBatchDrawFlag::None;
	if (bTiles)
	{
		const FSlateShaderResourceProxy* Proxy = Resource.GetResourceProxy();
		if (Proxy && Proxy->StartUV.IsNearlyZero() && Proxy->SizeUV.Equals(FVector2f(1.0f, 1.0f)))
		{
			BatchFlags = ESlateBatchDrawFlag::TileU | ESlateBatchDrawFlag::TileV;
		}
	}

	// RmlUi 6.x supplies vertex colours and generated texture data premultiplied and expects
	// the blend mode to match. Straight-alpha blending here is what produces the dark fringing
	// around text and translucent edges that reads as a filtering problem.
	FSlateDrawElement::MakeCustomVerts(
		*ElementList,
		LayerId,
		Resource,
		ScratchVertices,
		Compiled.Indices,
		nullptr, 0, 0,
		// A texture the game registered is straight alpha, so it takes Slate's straight blend and
		// the vertex colour above was handed over straight to match.
		bTextureStraightAlpha ? ESlateDrawEffect::None : ESlateDrawEffect::PreMultipliedAlpha,
		BatchFlags);

	MaxLayerId = FMath::Max(MaxLayerId, LayerId);
}

//
// Text held back from a supersampled surface
//

bool FFolioRenderInterface::ShouldDeferField() const
{
	// Not inside a layer: what a layer holds is read back by a filter, a mask or a shadow, and text
	// pulled out of it would be missing from the very thing that reads it.
	//
	// Not under a clip mask either, which is a stencil the surface owns and Slate cannot express.
	return bSurface && bDeferFields && !Layers.HasDocumentLayer() && !Layers.CurrentHasClipMask();
}

FSlateRect FFolioRenderInterface::BoundsOf(const TArray<FSlateVertex>& Vertices, const FVector2f Offset)
{
	if (Vertices.IsEmpty())
	{
		return FSlateRect();
	}

	FVector2f Min = FVector2f(Vertices[0].Position) + Offset;
	FVector2f Max = Min;

	for (const FSlateVertex& Vertex : Vertices)
	{
		const FVector2f Position = FVector2f(Vertex.Position) + Offset;
		Min = FVector2f::Min(Min, Position);
		Max = FVector2f::Max(Max, Position);
	}

	return FSlateRect(Min.X, Min.Y, Max.X, Max.Y);
}

void FFolioRenderInterface::ReturnFieldToLayer(const FDeferredField& Field, const int32 DepthFromTop)
{
	FMsdfBinding* Binding = FindMsdfBinding(Field.Page);
	if (!Binding)
	{
		return;
	}

	FVector4f FieldParams, EffectParams;
	PackMsdfParameters(*Binding, FieldParams, EffectParams);

	Layers.DrawFieldClipped(Field.Run.Vertices, Field.Run.Indices, Binding->Page->Texture.Get(),
		FVector2f::ZeroVector, FieldParams, EffectParams,
		Field.bScissored ? Field.Scissor : FIntRect(), DepthFromTop);
}

void FFolioRenderInterface::FlushCoveredFields(const FSlateRect& Bounds, const int32 DepthFromTop)
{
	// Held-back text belongs to the surface, so it only goes back when the surface is what the draw
	// asking is drawing into. Anywhere else is a layer of the document's own: a different target, and
	// one that is read back rather than composited straight to the screen.
	if (DeferredFields.IsEmpty() || !bSurface || Layers.Num() - 1 - DepthFromTop != 0)
	{
		return;
	}

	for (int32 Index = 0; Index < DeferredFields.Num(); )
	{
		FDeferredField& Field = DeferredFields[Index];

		if (!FSlateRect::DoRectanglesIntersect(Field.Bounds, Bounds))
		{
			++Index;
			continue;
		}

		// Only the glyphs the draw actually reaches. A run is a line of them and its bounds are the
		// whole line, so a card overlapping one word would otherwise cost the sentence.
		FDeferredField Covered;
		if (SplitCoveredGlyphs(Field, Bounds, Covered))
		{
			ReturnFieldToLayer(Covered, DepthFromTop);
			++Index;
			continue;
		}

		ReturnFieldToLayer(Field, DepthFromTop);

		// Order matters and this keeps it: what goes back goes back in the order it arrived.
		DeferredFields.RemoveAt(Index);
	}
}

bool FFolioRenderInterface::SplitCoveredGlyphs(FDeferredField& Field, const FSlateRect& Bounds,
	FDeferredField& OutCovered)
{
	// One quad per glyph, four vertices and six indices each, which is what RmlUi's font engine emits.
	// Anything else is not a run of glyphs and is handled whole.
	const int32 Quads = Field.Run.Vertices.Num() / 4;
	if (Quads < 2 || Field.Run.Vertices.Num() % 4 != 0 || Field.Run.Indices.Num() != Quads * 6)
	{
		return false;
	}

	TArray<FSlateVertex> KeptVertices;
	TArray<FSlateVertex> CoveredVertices;

	for (int32 Quad = 0; Quad < Quads; ++Quad)
	{
		const TArrayView<const FSlateVertex> Corners(&Field.Run.Vertices[Quad * 4], 4);

		FVector2f Min = FVector2f(Corners[0].Position);
		FVector2f Max = Min;

		for (const FSlateVertex& Corner : Corners)
		{
			Min = FVector2f::Min(Min, FVector2f(Corner.Position));
			Max = FVector2f::Max(Max, FVector2f(Corner.Position));
		}

		TArray<FSlateVertex>& Into = FSlateRect::DoRectanglesIntersect(
			FSlateRect(Min.X, Min.Y, Max.X, Max.Y), Bounds) ? CoveredVertices : KeptVertices;

		Into.Append(Corners.GetData(), 4);
	}

	if (CoveredVertices.IsEmpty() || KeptVertices.IsEmpty())
	{
		return false;
	}

	// Rebuilt rather than sliced: the two halves are no longer contiguous in the original.
	const auto QuadIndices = [](const int32 Count)
	{
		TArray<SlateIndex> Indices;
		Indices.Reserve(Count * 6);

		for (int32 Quad = 0; Quad < Count; ++Quad)
		{
			const SlateIndex Base = static_cast<SlateIndex>(Quad * 4);
			Indices.Append({ Base, SlateIndex(Base + 1), SlateIndex(Base + 2),
				Base, SlateIndex(Base + 2), SlateIndex(Base + 3) });
		}

		return Indices;
	};

	OutCovered = Field;
	OutCovered.Run.Vertices = MoveTemp(CoveredVertices);
	OutCovered.Run.Indices = QuadIndices(OutCovered.Run.Vertices.Num() / 4);

	Field.Run.Vertices = MoveTemp(KeptVertices);
	Field.Run.Indices = QuadIndices(Field.Run.Vertices.Num() / 4);
	Field.Bounds = BoundsOf(Field.Run.Vertices, FVector2f::ZeroVector);

	return true;
}

int32 FFolioRenderInterface::DrawDeferredFields(FSlateWindowElementList& InElementList,
	const FGeometry& InGeometry, const int32 InLayerId, TArray<FDeferredField>* OutRetain)
{
	if (DeferredFields.IsEmpty())
	{
		if (OutRetain)
		{
			OutRetain->Reset();
		}

		return InLayerId;
	}

	// Taken first: BeginFrame is the ordinary Slate entry point and the loop below draws through the
	// ordinary Slate paths, which read the state it installs.
	TArray<FDeferredField> Pending = MoveTemp(DeferredFields);
	DeferredFields.Reset();

	const int32 Highest = DrawFieldRuns(Pending, InElementList, InGeometry, InLayerId);

	if (OutRetain)
	{
		*OutRetain = MoveTemp(Pending);
	}

	return Highest;
}

int32 FFolioRenderInterface::DrawFieldRuns(const TArray<FDeferredField>& Pending,
	FSlateWindowElementList& InElementList, const FGeometry& InGeometry, const int32 InLayerId)
{
	if (Pending.IsEmpty())
	{
		return InLayerId;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_DeferredText);
	SCOPE_CYCLE_COUNTER(STAT_FolioDeferredText);

	const FIntPoint Size(FMath::RoundToInt(InGeometry.GetLocalSize().X),
		FMath::RoundToInt(InGeometry.GetLocalSize().Y));

	BeginFrame(InElementList, InGeometry, InLayerId, Size);

	for (const FDeferredField& Field : Pending)
	{
		const FMsdfBinding* Binding = ResolveMsdfBinding(Field.Page);
		if (!Binding)
		{
			continue;
		}

		bScissorEnabled = Field.bScissored;
		ScissorRegion = Rml::Rectanglei::FromCorners(
			Rml::Vector2i(Field.Scissor.Min.X, Field.Scissor.Min.Y),
			Rml::Vector2i(Field.Scissor.Max.X, Field.Scissor.Max.Y));

		ApplyClipping();

		// Positions are already in the document's space with the run's own offset and transform folded
		// in, so what is left is the widget's own geometry - which is what DrawGeometry applies.
		DrawGeometry(Field.Run, Rml::Vector2f(0.0f, 0.0f), Binding->Handle, FVector2f(1.0f, 1.0f),
			/*bStraightAlpha*/ true);
	}

	const int32 Highest = MaxLayerId;
	EndFrame();

	return Highest;
}

void FFolioRenderInterface::ReleaseGeometry(const Rml::CompiledGeometryHandle Handle)
{
	Geometries.Remove(Handle);
}

//
// Textures
//

FFolioRenderInterface::FTexture* FFolioRenderInterface::FindTexture(const Rml::TextureHandle Handle)
{
	const TUniquePtr<FTexture>* Found = Textures.Find(Handle);
	return Found && Found->IsValid() ? Found->Get() : nullptr;
}

UTexture2D* FFolioRenderInterface::CreateTexture(const void* Bgra, const FIntPoint Size)
{
	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	// UI textures are authored at their display size and atlased; filtering and mips would
	// only soften them.
	Texture->SRGB = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->NeverStream = true;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Bgra, static_cast<SIZE_T>(Size.X) * Size.Y * 4);
	Mip.BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}

UTexture2D* FFolioRenderInterface::GetWhiteTexture()
{
	// Separate from the Slate brush on purpose. The layer path draws with RHI and binds a real
	// texture; Slate's path wants a resource handle and gets one from a dynamic brush, which is
	// what keeps it off the UTexture branch that ensures on the render thread.
	if (!WhiteTexture.IsValid())
	{
		const uint8 Opaque[4] = { 255, 255, 255, 255 };
		WhiteTexture.Reset(CreateTexture(Opaque, FIntPoint(1, 1)));
	}

	return WhiteTexture.Get();
}

Rml::TextureHandle FFolioRenderInterface::RegisterRenderTarget(UTextureRenderTarget2D* Target)
{
	if (!Target)
	{
		return 0;
	}

	return RegisterTexture(Target, FIntPoint(Target->SizeX, Target->SizeY));
}

TSharedPtr<FSlateDynamicImageBrush> FFolioRenderInterface::MakeDynamicBrush(const FString& Name,
	const void* Bgra, const FIntPoint Size)
{
	// A dynamic image resource rather than a UTexture brush.
	//
	// Slate resolves a UTexture brush through FSlateBaseUTextureResource, whose AccessRHIResource
	// stamps LastRenderTime with FApp::GetCurrentTime - a game-thread call, made from the render
	// thread while batches are built, which ensures every frame a document is drawn. A dynamic
	// resource is native and takes a different branch entirely. It also costs no UObject, which
	// these never needed: they are render resources, not assets.
	if (!FSlateApplication::IsInitialized() || Bgra == nullptr || Size.X <= 0 || Size.Y <= 0)
	{
		return nullptr;
	}

	const int32 Bytes = Size.X * Size.Y * 4;
	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(Bytes);
	FMemory::Memcpy(Pixels.GetData(), Bgra, Bytes);

	// The brush owns the resource and releases it when the last reference goes, which is why it is
	// held by pointer rather than sliced into a plain FSlateBrush.
	return FSlateDynamicImageBrush::CreateWithImageData(FName(*Name),
		FVector2D(double(Size.X), double(Size.Y)), Pixels);
}

Rml::TextureHandle FFolioRenderInterface::RegisterPixels(const void* Bgra, const FIntPoint Size)
{
	const Rml::TextureHandle Handle = NextHandle++;

	TSharedPtr<FSlateDynamicImageBrush> Brush = MakeDynamicBrush(
		FString::Printf(TEXT("Folio.Tex.%llu"), static_cast<uint64>(Handle)), Bgra, Size);
	if (!Brush.IsValid())
	{
		return 0;
	}

	TUniquePtr<FTexture> Entry = MakeUnique<FTexture>();
	Entry->DynamicBrush = Brush;
	Entry->Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush);

	// A second copy as a UTexture, because a Slate resource handle is only good on Slate's path. The
	// recording and layer replays bind an FRHITexture straight off a UTexture, so without this every
	// generated image - which is every gradient - resolves to nothing and the shader samples white.
	// That is not a fallback anyone sees as missing: a white quad the size of the element.
	if (UTexture2D* Generated = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8))
	{
		Generated->SRGB = true;
		Generated->Filter = TF_Bilinear;
		Generated->AddressX = TA_Clamp;
		Generated->AddressY = TA_Clamp;
		Generated->NeverStream = true;

		FTexture2DMipMap& Mip = Generated->GetPlatformData()->Mips[0];
		if (void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE))
		{
			FMemory::Memcpy(Data, Bgra, static_cast<SIZE_T>(Size.X) * Size.Y * 4);
		}
		Mip.BulkData.Unlock();

		// Ordered rather than flushed: this enqueues the resource init, and whatever draws with it
		// enqueues afterwards, so the texture is always ready by the time the pass runs.
		Generated->UpdateResource();
		Entry->Texture.Reset(Generated);
	}

	Textures.Add(Handle, MoveTemp(Entry));
	return Handle;
}

Rml::TextureHandle FFolioRenderInterface::RegisterTexture(UTexture* Texture, const FIntPoint Size,
	const bool bStraightAlpha)
{
	if (!Texture)
	{
		return 0;
	}

	TUniquePtr<FTexture> Entry = MakeUnique<FTexture>();
	Entry->Texture.Reset(Texture);
	Entry->Brush.SetResourceObject(Texture);
	Entry->Brush.ImageSize = FVector2f(Size.X, Size.Y);
	Entry->Brush.DrawAs = ESlateBrushDrawType::Image;
	Entry->bStraightAlpha = bStraightAlpha;

	if (FSlateApplication::IsInitialized())
	{
		Entry->Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry->Brush);
	}

	const Rml::TextureHandle Handle = NextHandle++;
	Textures.Add(Handle, MoveTemp(Entry));
	return Handle;
}

Rml::TextureHandle FFolioRenderInterface::LoadTexture(Rml::Vector2i& OutDimensions, const Rml::String& Source)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_LoadTexture);
	SCOPE_CYCLE_COUNTER(STAT_FolioLoadTexture);
	LLM_SCOPE_BYTAG(Folio);

	// A texture the game registered rather than a file. Resolved before the file interface, because
	// there is no file for it to open and a scene capture has no path.
	if (const FName Named = Folio::ParseTextureName(Folio::ToFString(Source)); !Named.IsNone())
	{
		UTexture* Texture = Folio::FindTexture(Named);
		if (!Texture)
		{
			UE_LOG(LogFolio, Warning,
				TEXT("'%s' names a texture the game has not registered. Call Folio::RegisterTexture, or ")
				TEXT("UFolioWidget::RegisterTexture, before the document draws."),
				*Folio::ToFString(Source));
			return 0;
		}

		const FIntPoint Size(
			FMath::Max(1, int32(Texture->GetSurfaceWidth())),
			FMath::Max(1, int32(Texture->GetSurfaceHeight())));

		OutDimensions = Rml::Vector2i(Size.X, Size.Y);

		// An engine texture as the game imported it. Nothing here premultiplied it, and an icon drawn
		// as though it were adds the colour its transparent pixels happen to hold over whatever is
		// behind the element, which reads as a wash the size of the element box.
		return RegisterTexture(Texture, Size, /*bStraightAlpha*/ true);
	}

	Rml::FileInterface* Files = Rml::GetFileInterface();
	const Rml::FileHandle File = Files ? Files->Open(Source) : Rml::FileHandle();
	if (!File)
	{
		UE_LOG(LogFolio, Warning, TEXT("could not open texture '%s'"), *Folio::ToFString(Source));
		return 0;
	}

	const size_t Length = Files->Length(File);
	TArray<uint8> Encoded;
	Encoded.SetNumUninitialized(static_cast<int32>(Length));
	Files->Read(Encoded.GetData(), Length, File);
	Files->Close(File);

	// RmlUi's own loader reads uncompressed TGA and nothing else, which no real atlas is.
	// The engine's image wrappers cover PNG, TGA, JPEG and the rest, so the format question
	// stops being ours.
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	const EImageFormat Format = ImageWrapperModule.DetectImageFormat(Encoded.GetData(), Encoded.Num());
	const TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);

	TArray64<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Encoded.GetData(), Encoded.Num()) ||
		!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		UE_LOG(LogFolio, Warning, TEXT("could not decode texture '%s'"), *Folio::ToFString(Source));
		return 0;
	}

	const FIntPoint Size(Wrapper->GetWidth(), Wrapper->GetHeight());

	OutDimensions = Rml::Vector2i(Size.X, Size.Y);

	LoadedTextureTimes.Add(Folio::ToFString(Source), TextureFileTime(Source));

	// A sheet baked by msdfbake carries a sidecar naming its field parameters. With it the image
	// is distance rather than colour, and a sprite cut from it scales like text does.
	//
	// Deliberately before premultiplying: a field's alpha is a distance, not an opacity, and
	// scaling the three channels by it erases the field wherever the shape is thin.
	if (const Rml::TextureHandle Field = LoadFieldAtlas(Source, Raw, Size))
	{
		return Field;
	}

	// Everything downstream assumes premultiplied alpha; decoded images are straight.
	for (int64 Index = 0; Index + 3 < Raw.Num(); Index += 4)
	{
		const uint32 Alpha = Raw[Index + 3];
		Raw[Index + 0] = static_cast<uint8>((Raw[Index + 0] * Alpha + 127) / 255);
		Raw[Index + 1] = static_cast<uint8>((Raw[Index + 1] * Alpha + 127) / 255);
		Raw[Index + 2] = static_cast<uint8>((Raw[Index + 2] * Alpha + 127) / 255);
	}

	return RegisterPixels(Raw.GetData(), Size);
}

Rml::TextureHandle FFolioRenderInterface::GenerateTexture(const Rml::Span<const Rml::byte> Source,
	const Rml::Vector2i Dimensions)
{
	const FIntPoint Size(Dimensions.x, Dimensions.y);

	// RmlUi generates RGBA and already premultiplied; the engine wants BGRA.
	TArray<uint8> Bgra;
	Bgra.SetNumUninitialized(Size.X * Size.Y * 4);
	for (int32 Index = 0; Index + 3 < Bgra.Num(); Index += 4)
	{
		Bgra[Index + 0] = Source[Index + 2];
		Bgra[Index + 1] = Source[Index + 1];
		Bgra[Index + 2] = Source[Index + 0];
		Bgra[Index + 3] = Source[Index + 3];
	}

	return RegisterPixels(Bgra.GetData(), Size);
}

FDateTime FFolioRenderInterface::TextureFileTime(const Rml::String& Source)
{
	FFolioFileInterface* Files = static_cast<FFolioFileInterface*>(Rml::GetFileInterface());
	if (!Files)
	{
		return FDateTime();
	}

	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();

	FDateTime Newest = Platform.GetTimeStamp(*Files->Resolve(Source));

	const std::string Sidecar = Folio::MsdfSidecarPathFor(Source);
	if (!Sidecar.empty())
	{
		const FDateTime SidecarTime = Platform.GetTimeStamp(*Files->Resolve(Sidecar));
		if (SidecarTime > Newest)
		{
			Newest = SidecarTime;
		}
	}

	return Newest;
}

int32 FFolioRenderInterface::ReloadChangedTextures(const bool bForce)
{
	TArray<FString> Stale;
	for (const TPair<FString, FDateTime>& Loaded : LoadedTextureTimes)
	{
		if (bForce || TextureFileTime(Folio::ToRmlString(Loaded.Key)) != Loaded.Value)
		{
			Stale.Add(Loaded.Key);
		}
	}

	for (const FString& Source : Stale)
	{
		// Through RmlUi, so its own cache lets go and the document's decorators re-request a
		// handle. Dropping our entry alone would leave every one of them holding the old one.
		Rml::ReleaseTexture(Folio::ToRmlString(Source), this);
		LoadedTextureTimes.Remove(Source);
	}

	return Stale.Num();
}

UTexture* FFolioRenderInterface::ResolveFillTexture(const std::string& Path)
{
	if (Path.empty())
	{
		return nullptr;
	}

	const FString Key = Folio::ToFString(Path);
	if (TStrongObjectPtr<UTexture>* Found = FillTextures.Find(Key))
	{
		return Found->Get();
	}

	// Through the ordinary texture path, so a pattern is loaded and decoded exactly like any other
	// piece of art. The handle it hands back is one of ours, so the texture is fished back out of
	// the map rather than reconstructed.
	Rml::Vector2i Size;
	const Rml::TextureHandle Handle = LoadTexture(Size, Path);

	UTexture* Texture = nullptr;
	if (const FTexture* Entry = FindTexture(Handle))
	{
		Texture = Entry->Texture.Get();
	}

	if (Texture == nullptr)
	{
		// A dynamic brush owns its pixels through Slate rather than a UTexture, and the material
		// needs a UTexture. Nothing is lost by saying so: the text still draws, just flat.
		UE_LOG(LogFolio, Warning,
			TEXT("could not use '%s' as a fill pattern; the text is drawn flat"), *Key);
	}

	FillTextures.Add(Key, TStrongObjectPtr<UTexture>(Texture));
	return Texture;
}

void FFolioRenderInterface::ReleaseTexture(const Rml::TextureHandle Handle)
{
	// A dynamic brush releases its own Slate resource once the last reference to it goes.
	Textures.Remove(Handle);

	// A glyph atlas is not in either map: the font engine owns it and releases it through
	// ReleaseAtlasHandle. A baked sheet loaded from a file is RmlUi's, and arrives here.
	if (FieldAtlasesFromFile.Remove(Handle) > 0)
	{
		MsdfBindings.Remove(Handle);
	}
}

//
// Clipping and transforms
//

void FFolioRenderInterface::EnableScissorRegion(const bool bEnable)
{
	bScissorEnabled = bEnable;
	ApplyRecordedScissor();
}

void FFolioRenderInterface::SetScissorRegion(const Rml::Rectanglei Region)
{
	ScissorRegion = Region;
	ApplyRecordedScissor();
}

/**
 * The offscreen twin of ApplyClipping.
 *
 * A recording has no element list to push a clip zone onto, so the scissor travels with each draw
 * instead. Layer space and scissor space share an origin, so the rectangle needs only the layer's
 * own offset taken off it.
 */
void FFolioRenderInterface::ApplyRecordedScissor()
{
	FFolioDrawList* Recording = DrawList ? DrawList : Layers.CurrentDrawList();
	if (!Recording)
	{
		return;
	}

	if (!bScissorEnabled)
	{
		Recording->SetScissor(FIntRect());
		return;
	}

	Recording->SetScissor(FIntRect(
		ScissorRegion.Left() - LayerOrigin.X, ScissorRegion.Top() - LayerOrigin.Y,
		ScissorRegion.Right() - LayerOrigin.X, ScissorRegion.Bottom() - LayerOrigin.Y));
}

void FFolioRenderInterface::ApplyClipping()
{
	if (!ElementList)
	{
		return;
	}

	if (bClipPushed)
	{
		ElementList->PopClip();
		bClipPushed = false;
	}

	const FSlateRenderTransform& ToWindow = Geometry.GetAccumulatedRenderTransform();

	TOptional<FSlateRect> Clip;

	if (bScissorEnabled)
	{
		// RmlUi's scissor is in window coordinates with a top-left origin and is deliberately
		// unaffected by SetTransform, so it composes with the widget's geometry only.
		const FVector2f Position(static_cast<float>(ScissorRegion.Left()), static_cast<float>(ScissorRegion.Top()));
		const FVector2f Size(static_cast<float>(ScissorRegion.Width()), static_cast<float>(ScissorRegion.Height()));

		const FVector2f TopLeft(TransformPoint(ToWindow, Position));
		const FVector2f BottomRight(TransformPoint(ToWindow, Position + Size));
		Clip = FSlateRect(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);
	}

	if (ClipMaskRect.IsSet())
	{
		// The mask is in document space and the scissor is already in window space, so the
		// mask is transformed and then intersected: both are clips, and Slate has one stack.
		const FVector2f TopLeft(TransformPoint(ToWindow, FVector2f(ClipMaskRect->Left, ClipMaskRect->Top)));
		const FVector2f BottomRight(TransformPoint(ToWindow, FVector2f(ClipMaskRect->Right, ClipMaskRect->Bottom)));
		const FSlateRect MaskInWindow(TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);

		Clip = Clip.IsSet() ? Clip.GetValue().IntersectionWith(MaskInWindow) : MaskInWindow;
	}

	if (!Clip.IsSet())
	{
		return;
	}

	ElementList->PushClip(FSlateClippingZone(Clip.GetValue()));
	bClipPushed = true;
}

void FFolioRenderInterface::SetTransform(const Rml::Matrix4f* Transform)
{
	if (!Transform)
	{
		bHasTransform = false;
		RmlTransform = FSlateRenderTransform();
		return;
	}

	// Slate's render transform is 2D. RCSS 3D transforms are out of scope by decision, so the
	// upper-left 2x2 plus the translation column is the whole of what can be expressed; a
	// perspective or rotateX would silently flatten, which is why they are banned in the
	// conventions rather than merely discouraged.
	const Rml::Matrix4f& M = *Transform;
	const FMatrix2x2 Linear(
		M[0][0], M[0][1],
		M[1][0], M[1][1]);
	const FVector2f Offset(M[3][0], M[3][1]);

	RmlTransform = FSlateRenderTransform(Linear, Offset);
	bHasTransform = true;
}

//
// Clip masks
//

void FFolioRenderInterface::EnableClipMask(const bool bEnable)
{
	// A stencil belongs to a layer, and a recording has none. Clipping a world-space document is the
	// surface's business rather than the document's.
	if (DrawList)
	{
		return;
	}

	if (!Layers.IsEmpty())
	{
		Layers.EnableClipMask(bEnable);
		return;
	}

	if (!bEnable)
	{
		ClipMaskRect.Reset();
	}
}

void FFolioRenderInterface::RenderToClipMask(const Rml::ClipMaskOperation Operation,
	const Rml::CompiledGeometryHandle GeometryHandle, const Rml::Vector2f Translation)
{
	if (DrawList)
	{
		return;
	}

	const TUniquePtr<FCompiledGeometry>* Found = Geometries.Find(GeometryHandle);
	if (!Found || !Found->IsValid() || (*Found)->Vertices.IsEmpty())
	{
		return;
	}

	// Inside a layer the mask is a real stencil, so the shape is exact and SetInverse works.
	if (!Layers.IsEmpty())
	{
		Layers.SetClipMask(Operation, (*Found)->Vertices, (*Found)->Indices,
			FVector2f(Translation.x - LayerOrigin.X, Translation.y - LayerOrigin.Y));
		return;
	}

	// Outside one, Slate clips with a quad, so the mask becomes the bounds of the geometry
	// that drew it. Exact for a rectangle; a rounded rectangle clips to its square corners.
	FVector2f Minimum((*Found)->Vertices[0].Position);
	FVector2f Maximum = Minimum;
	for (const FSlateVertex& Vertex : (*Found)->Vertices)
	{
		Minimum = FVector2f::Min(Minimum, FVector2f(Vertex.Position));
		Maximum = FVector2f::Max(Maximum, FVector2f(Vertex.Position));
	}

	const FVector2f Offset(Translation.x, Translation.y);
	const FSlateRect Bounds(Minimum.X + Offset.X, Minimum.Y + Offset.Y, Maximum.X + Offset.X, Maximum.Y + Offset.Y);

	switch (Operation)
	{
	case Rml::ClipMaskOperation::Set:
	case Rml::ClipMaskOperation::SetInverse:
		// An inverse mask cannot be expressed as a quad at all. Setting the bounds rather
		// than inverting them keeps the element visible, which is the lesser wrong.
		ClipMaskRect = Bounds;
		break;

	case Rml::ClipMaskOperation::Intersect:
		ClipMaskRect = ClipMaskRect.IsSet() ? ClipMaskRect.GetValue().IntersectionWith(Bounds) : Bounds;
		break;
	}
}

//
// Layers
//

Rml::LayerHandle FFolioRenderInterface::PushLayer()
{
	// A layer is an offscreen target composited back into whatever is below it, and a recording has
	// nothing below it. Refusing the layer means the effect that wanted it draws nothing rather than
	// drawing into a target nobody reads - and the geometry inside it still reaches the recording,
	// because every draw path falls through to the recording when no layer is open.
	if (DrawList)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogFolio, Warning,
				TEXT("a world-space document used a layer effect - box-shadow, filter, backdrop-filter or "
					"mask-image - which needs an offscreen target this path does not have. The effect is "
					"skipped and the element still draws. Use a render-target surface for a document that "
					"needs them."));
		}

		RecordedLayerMarks.Add(DrawList->Num());
		return 0;
	}

	// A layer over another one matches it, because the two share a space: geometry is recorded against
	// LayerOrigin, which the bottom layer fixed, and compositing covers the whole target. Sizing this
	// one from the scissor instead puts it in a different space to the layer below - and by now the
	// scissor is the element's own clip, so it is a different space every time.
	//
	// mask-image is where that shows, since the mask and what it masks are read at the same coordinate.
	if (Layers.HasDocumentLayer())
	{
		if (!Layers.PushMatchingTop())
		{
			UE_LOG(LogFolio, Warning, TEXT("could not allocate a nested layer"));
		}

		return static_cast<Rml::LayerHandle>(++LayerDepth);
	}

	// The bottom layer covers the current scissor region, which is what RmlUi has already clamped to
	// the window. Allocating the whole context instead would make a small shadow cost a
	// full-screen target.
	FIntPoint Size = ContextSize;
	FIntPoint Origin = FIntPoint::ZeroValue;

	if (bScissorEnabled && ScissorRegion.Width() > 0 && ScissorRegion.Height() > 0)
	{
		Size = FIntPoint(ScissorRegion.Width(), ScissorRegion.Height());
		Origin = FIntPoint(ScissorRegion.Left(), ScissorRegion.Top());
	}

	LayerOrigin = Origin;

	if (!Layers.Push(Size, Origin))
	{
		UE_LOG(LogFolio, Warning, TEXT("could not allocate a %dx%d layer"), Size.X, Size.Y);
	}

	return static_cast<Rml::LayerHandle>(++LayerDepth);
}

void FFolioRenderInterface::CompositeLayers(const Rml::LayerHandle Source, const Rml::LayerHandle Destination,
	const Rml::BlendMode BlendMode, const Rml::Span<const Rml::CompiledFilterHandle> AppliedFilters)
{
	if (DrawList)
	{
		return;
	}

	// Filters apply to the layer after it resolves and before it composites, which is the
	// order that makes a blurred shadow a blurred shadow rather than a blurred everything.
	float Sigma = 0.0f;
	bool bOutside = false;
	TArray<Folio::FColourFilter> Colour;
	UTextureRenderTarget2D* Mask = nullptr;

	for (const Rml::CompiledFilterHandle Handle : AppliedFilters)
	{
		if (const FFilter* Filter = Filters.Find(Handle))
		{
			Sigma = FMath::Max(Sigma, Filter->Sigma);
			bOutside = bOutside || Filter->bOutside;

			if (Filter->Mask.IsValid())
			{
				Mask = Filter->Mask.Get();
			}

			// Order is kept, because `saturate(0) brightness(2)` and `brightness(2) saturate(0)` are
			// different pictures and CSS says the first named runs first.
			if (Filter->Colour.IsSet())
			{
				Colour.Add(Filter->Colour.GetValue());
			}
		}
	}

	// A composite that reads from a layer below the top is backdrop-filter asking for whatever is
	// already on screen. Nothing we own has it: the backdrop is the game, the editor viewport and
	// any UMG under us, all of which went straight into Slate's own targets.
	//
	// Slate can do it though. A post-process element blurs the backbuffer in place, which is the
	// only way to blur the 3D scene behind a HUD panel and is exactly what SBackgroundBlur is.
	//
	// Deferred to the second of the pair, because the first runs with the scissor widened to
	// include everything the filter reads and only the second is clipped to the element itself.
	// Blurring on the first bleeds a rectangle of blur out past the panel's edges.
	if (Source < Destination)
	{
		PendingBackdropSigma = Sigma;
		bPendingBackdropOutside = bOutside;
		return;
	}

	if (PendingBackdropSigma > 0.0f)
	{
		BlurBackdrop(PendingBackdropSigma, bPendingBackdropOutside);
		PendingBackdropSigma = 0.0f;
		bPendingBackdropOutside = false;
		return;
	}

	// Below the bottom layer is Slate, not another target.
	if (Layers.Num() <= 1)
	{
		CompositeLayerToSlate(Sigma, Colour, Mask);
		return;
	}

	// What this layer painted is about to be drawn over the layer below, so held-back text underneath
	// it goes back first. What it painted, not the region it was allocated: an unclipped element gets
	// a layer the size of the whole context, and that would put every glyph run on the screen back.
	//
	// Widened by the blur, which is the one thing that puts colour outside the geometry that made it.
	if (const FSlateRect Painted = Layers.CurrentContentRect(); Painted.IsValid())
	{
		FlushCoveredFields(Sigma > 0.0f ? Painted.ExtendBy(FMargin(3.0f * Sigma)) : Painted,
			/*DepthFromTop*/ 1);
	}

	Layers.CompositeDown(BlendMode == Rml::BlendMode::Blend, Sigma, Colour, Mask);
}

void FFolioRenderInterface::BlurBackdrop(const float Sigma, const bool bOutside)
{
	if (!ElementList || Sigma <= 0.0f)
	{
		return;
	}

	// RmlUi has already set the scissor to the element's border box plus whatever the filter reads
	// beyond it, so the region to blur is the region we are clipped to.
	const FVector2f Origin = bScissorEnabled
		? FVector2f(float(ScissorRegion.Left()), float(ScissorRegion.Top()))
		: FVector2f(0.0f, 0.0f);
	const FVector2f Size = bScissorEnabled
		? FVector2f(float(ScissorRegion.Width()), float(ScissorRegion.Height()))
		: FVector2f(float(ContextSize.X), float(ContextSize.Y));

	if (Size.X < 1.0f || Size.Y < 1.0f)
	{
		return;
	}

	// Kernel and downsampling follow SBackgroundBlur: past nine taps the cost is better spent on a
	// smaller target, and the strength is divided to match so the result looks the same.
	int32 Kernel = FMath::RoundToInt(Sigma * 3.0f);
	int32 Downsample = 0;

	if (Kernel > 9)
	{
		Downsample = Kernel >= 64 ? (Kernel >= 96 ? 4 : 3) : 2;
		Kernel /= Downsample;
	}

	if (Kernel % 2 == 0)
	{
		++Kernel;
	}
	Kernel = FMath::Clamp(Kernel, 3, 255);

	const float Strength = FMath::Max(0.5f, Sigma);
	const FSlateRenderTransform Transform = Geometry.GetAccumulatedRenderTransform();

	const auto Blur = [&](const FVector2f BandOrigin, const FVector2f BandSize)
	{
		if (BandSize.X < 1.0f || BandSize.Y < 1.0f)
		{
			return;
		}

		// Per band, because the downsampled target is sized from the band and a shared one would
		// stretch each band differently.
		FIntPoint Target(FMath::RoundToInt(BandSize.X), FMath::RoundToInt(BandSize.Y));
		float BandStrength = Strength;
		if (Downsample > 0)
		{
			Target = FIntPoint(FMath::DivideAndRoundUp(Target.X, Downsample),
				FMath::DivideAndRoundUp(Target.Y, Downsample));
			BandStrength /= float(Downsample);
		}

		const FVector2f TopLeft = TransformPoint(Transform, BandOrigin);
		const FVector2f BottomRight = TransformPoint(Transform, BandOrigin + BandSize);

		FSlateDrawElement::MakePostProcessBlur(*ElementList, LayerId,
			FPaintGeometry(FVector2D(TopLeft), FVector2D(BottomRight - TopLeft), 1.0f),
			FVector4f(float(Kernel), BandStrength, float(Target.X), float(Target.Y)), Downsample);
	};

	if (!bOutside)
	{
		ApplyClipping();
		Blur(Origin, Size);
	}
	else
	{
		// The scrim case: the element is a hole and everything else is blurred. Four bands around
		// it, because a post-process element is a rectangle and there is no way to cut one out of
		// another.
		//
		//   +-----------------------+
		//   |         above         |
		//   +------+--------+-------+
		//   | left |  hole  | right |
		//   +------+--------+-------+
		//   |         below         |
		//   +-----------------------+
		//
		// No ApplyClipping here, and any clip already on the stack comes off: it is the element's
		// own scissor, and every band is outside it. Clipped, all four would draw nothing.
		if (bClipPushed)
		{
			ElementList->PopClip();
			bClipPushed = false;
		}

		const float Right = Origin.X + Size.X;
		const float Bottom = Origin.Y + Size.Y;
		const FVector2f View(float(ContextSize.X), float(ContextSize.Y));

		Blur(FVector2f(0.0f, 0.0f), FVector2f(View.X, Origin.Y));
		Blur(FVector2f(0.0f, Bottom), FVector2f(View.X, View.Y - Bottom));
		Blur(FVector2f(0.0f, Origin.Y), FVector2f(Origin.X, Size.Y));
		Blur(FVector2f(Right, Origin.Y), FVector2f(View.X - Right, Size.Y));
	}

	// Everything after this has to land in a higher Slate layer, or it is drawn into the same batch
	// the post-process reads and comes back blurred along with the backdrop - including the panel's
	// own text, which is the first thing anyone notices.
	++LayerId;
	MaxLayerId = FMath::Max(MaxLayerId, LayerId);
}

void FFolioRenderInterface::CompositeLayerToSlate(const float BlurSigma,
	const TArrayView<const Folio::FColourFilter> Colour, UTextureRenderTarget2D* Mask)
{
	UTextureRenderTarget2D* Target = Layers.DetachCurrent(BlurSigma, Colour, Mask);
	if (!Target || !ElementList || !FSlateApplication::IsInitialized())
	{
		return;
	}

	FLayerComposite& Composite = LayerComposites.AddDefaulted_GetRef();
	Composite.Target.Reset(Target);
	Composite.Brush.SetResourceObject(Target);
	Composite.Brush.ImageSize = FVector2f(Target->SizeX, Target->SizeY);
	Composite.Brush.DrawAs = ESlateBrushDrawType::Image;
	Composite.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Composite.Brush);

	if (!Composite.Handle.IsValid())
	{
		return;
	}

	const FVector2f Origin(LayerOrigin.X, LayerOrigin.Y);
	const FVector2f Size(Target->SizeX, Target->SizeY);

	FCompiledGeometry Quad;
	Quad.Vertices.Reserve(4);

	auto AddCorner = [&Quad](const FVector2f Position, const FVector2f Uv)
	{
		FSlateVertex& Out = Quad.Vertices.AddDefaulted_GetRef();
		Out.Position = Position;
		Out.TexCoords[0] = Uv.X;
		Out.TexCoords[1] = Uv.Y;
		Out.TexCoords[2] = 1.0f;
		Out.TexCoords[3] = 1.0f;
		Out.MaterialTexCoords = Uv;
		Out.Color = FColor::White;
	};

	AddCorner(Origin, FVector2f(0.0f, 0.0f));
	AddCorner(FVector2f(Origin.X + Size.X, Origin.Y), FVector2f(1.0f, 0.0f));
	AddCorner(Origin + Size, FVector2f(1.0f, 1.0f));
	AddCorner(FVector2f(Origin.X, Origin.Y + Size.Y), FVector2f(0.0f, 1.0f));

	Quad.Indices = { 0, 1, 2, 0, 2, 3 };

	// The layer's contents already carry whatever transform RmlUi had when they were recorded,
	// so this quad must not have it applied a second time.
	const bool bSavedTransform = bHasTransform;
	bHasTransform = false;

	ApplyClipping();
	DrawGeometry(Quad, Rml::Vector2f(0.0f, 0.0f), Composite.Handle, FVector2f(1.0f, 1.0f));

	bHasTransform = bSavedTransform;
}

void FFolioRenderInterface::PopLayer()
{
	if (DrawList)
	{
		if (!RecordedLayerMarks.IsEmpty())
		{
			RecordedLayerMarks.Pop();
		}
		return;
	}

	Layers.Pop();
	LayerDepth = FMath::Max(0, LayerDepth - 1);

	// Back to whatever the layer below records against, which for a surface is its own target.
	LayerOrigin = Layers.CurrentOrigin();
}

Rml::TextureHandle FFolioRenderInterface::GetTransparentTexture()
{
	if (TransparentTexture != 0)
	{
		return TransparentTexture;
	}

	const uint8 Clear[4] = { 0, 0, 0, 0 };
	TransparentTexture = RegisterPixels(Clear, FIntPoint(1, 1));
	return TransparentTexture;
}

Rml::TextureHandle FFolioRenderInterface::SaveLayerAsTexture()
{
	UTextureRenderTarget2D* Detached = Layers.DetachCurrent();

	if (!Detached)
	{
		// No layer to read back. Transparent rather than zero: zero also means "no texture"
		// on the geometry path, so the caller's quad would paint white over its own element.
		return GetTransparentTexture();
	}

	return RegisterRenderTarget(Detached);
}

Rml::CompiledFilterHandle FFolioRenderInterface::SaveLayerAsMaskImage()
{
	// The mask is a whole layer, and it arrives as a filter rather than as a clip mask. That is what
	// makes it a real alpha mask: the stencil path records coverage, so a gradient or a soft-edged
	// image used as a mask would come out with hard edges.
	FFilter Filter;
	Filter.Name = "mask-image";

	if (DrawList)
	{
		// No layer to detach, and the mask decorators went into the recording. They are not the
		// element and nothing will consume them, so they come back out; leaving them paints the mask
		// itself over what it was meant to mask. The filter is still registered, so the release RmlUi
		// pairs with this has something to find.
		if (!RecordedLayerMarks.IsEmpty())
		{
			DrawList->GetDraws().SetNum(RecordedLayerMarks.Last(), EAllowShrinking::No);
		}
	}
	else
	{
		Filter.Mask.Reset(Layers.DetachCurrent());
	}

	const Rml::CompiledFilterHandle Handle = NextHandle++;
	Filters.Add(Handle, MoveTemp(Filter));
	return Handle;
}

namespace
{
	/** The RCSS names, which are CSS's, mapped onto the shader's modes. */
	const TMap<FString, Folio::EColourFilter>& ColourFilterNames()
	{
		static const TMap<FString, Folio::EColourFilter> Names = {
			{ TEXT("brightness"), Folio::EColourFilter::Brightness },
			{ TEXT("contrast"),   Folio::EColourFilter::Contrast },
			{ TEXT("grayscale"),  Folio::EColourFilter::Grayscale },
			{ TEXT("invert"),     Folio::EColourFilter::Invert },
			{ TEXT("opacity"),    Folio::EColourFilter::Opacity },
			{ TEXT("saturate"),   Folio::EColourFilter::Saturate },
			{ TEXT("sepia"),      Folio::EColourFilter::Sepia },
			{ TEXT("hue-rotate"), Folio::EColourFilter::HueRotate },
		};

		return Names;
	}
}

Rml::CompiledFilterHandle FFolioRenderInterface::CompileFilter(const Rml::String& Name,
	const Rml::Dictionary& Parameters)
{
	FFilter Filter;
	Filter.Name = Name;

	if (Name == "blur" || Name == "outside-blur")
	{
		// RmlUi's blur radius is a standard deviation already, in pixels.
		const auto It = Parameters.find("sigma");
		if (It != Parameters.end())
		{
			It->second.GetInto(Filter.Sigma);
		}

		// Folio's own. The element's box becomes a hole rather than the region blurred.
		Filter.bOutside = Name == "outside-blur";
	}
	else if (const Folio::EColourFilter* Kind = ColourFilterNames().Find(Folio::ToFString(Name)))
	{
		Folio::FColourFilter Colour;
		Colour.Kind = *Kind;

		// RmlUi normalises every one of these to a single "value" parameter, and hands hue-rotate's
		// over already converted to radians. The default is what CSS says the filter does with no
		// argument, which is not the same number for all of them: an invert of 1 inverts, a brightness
		// of 1 does nothing.
		Colour.Amount = *Kind == Folio::EColourFilter::Grayscale
			|| *Kind == Folio::EColourFilter::Invert
			|| *Kind == Folio::EColourFilter::Sepia
			|| *Kind == Folio::EColourFilter::HueRotate
			? 0.0f : 1.0f;

		const auto It = Parameters.find("value");
		if (It != Parameters.end())
		{
			It->second.GetInto(Colour.Amount);
		}

		Filter.Colour = Colour;
	}
	else
	{
		// std::string has no GetTypeHash, so the set is of FString.
		static TSet<FString> Reported;
		const FString Reported_Name = Folio::ToFString(Name);
		if (!Reported.Contains(Reported_Name))
		{
			Reported.Add(Reported_Name);
			UE_LOG(LogFolio, Warning, TEXT("filter '%s' is not implemented and renders as nothing"),
				*Reported_Name);
		}
	}

	const Rml::CompiledFilterHandle Handle = NextHandle++;
	Filters.Add(Handle, MoveTemp(Filter));
	return Handle;
}

void FFolioRenderInterface::ReleaseFilter(const Rml::CompiledFilterHandle Filter)
{
	Filters.Remove(Filter);
}

//
// Shaders
//

Rml::CompiledShaderHandle FFolioRenderInterface::CompileShader(const Rml::String& Name,
	const Rml::Dictionary& Parameters)
{
	TUniquePtr<FShader> Shader = MakeUnique<FShader>();

	// `decorator: shader("...")` - the effects RCSS has no syntax for. RmlUi hands the whole
	// string through untouched, which is the seam a backend would run its own fragment shader in;
	// these are rasterised instead, so both renderers produce the same picture.
	if (Name == "shader")
	{
		Rml::String Value;
		if (const auto It = Parameters.find("value"); It != Parameters.end())
		{
			It->second.GetInto(Value);
		}

		Rml::Vector2f Dimensions(1.0f, 1.0f);
		if (const auto It = Parameters.find("dimensions"); It != Parameters.end())
		{
			It->second.GetInto(Dimensions);
		}

		if (!Folio::ParseShaderEffect(Value, Shader->Effect))
		{
			UE_LOG(LogFolio, Warning,
				TEXT("shader('%s') is not one Folio knows; it renders as nothing. Try additive, "
					"scanlines or sweep."), *Folio::ToFString(Value));
			return 0;
		}

		Shader->Dimensions = FIntPoint(FMath::Max(1, FMath::RoundToInt(Dimensions.x)),
			FMath::Max(1, FMath::RoundToInt(Dimensions.y)));

		const Rml::CompiledShaderHandle EffectHandle = NextHandle++;
		Shaders.Add(EffectHandle, MoveTemp(Shader));
		return EffectHandle;
	}

	Folio::FGradient Gradient;
	if (!Folio::ParseGradient(Name, Parameters, Gradient))
	{
		UE_LOG(LogFolio, Warning, TEXT("unsupported shader '%s'"), *Folio::ToFString(Name));
		return 0;
	}

	Shader->Gradient = MoveTemp(Gradient);

	const Rml::CompiledShaderHandle Handle = NextHandle++;
	Shaders.Add(Handle, MoveTemp(Shader));
	return Handle;
}

const FFolioRenderInterface::FTexture* FFolioRenderInterface::GetGradientTexture(
	const Rml::CompiledShaderHandle Handle, const FVector2f BoxSize)
{
	const TUniquePtr<FShader>* Found = Shaders.Find(Handle);
	if (!Found || !Found->IsValid())
	{
		return nullptr;
	}

	FShader& Shader = **Found;

	// A gradient is smooth, so the texture does not have to match the element pixel for
	// pixel. Capping it keeps a full-screen background from allocating a full-screen texture
	// while staying well inside what bilinear filtering hides.
	static constexpr int32 MaxSize = 256;
	const FIntPoint TextureSize(
		FMath::Clamp(FMath::CeilToInt(BoxSize.X), 1, MaxSize),
		FMath::Clamp(FMath::CeilToInt(BoxSize.Y), 1, MaxSize));

	if (const Rml::TextureHandle* Cached = Shader.Baked.Find(TextureSize))
	{
		return FindTexture(*Cached);
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_BakeGradient);
	SCOPE_CYCLE_COUNTER(STAT_FolioBakeDecorator);

	TArray<uint8> Bgra;
	Folio::RasterizeGradient(Shader.Gradient, BoxSize, TextureSize, Bgra);

	const Rml::TextureHandle Baked = RegisterPixels(Bgra.GetData(), TextureSize);
	if (Baked == 0)
	{
		return nullptr;
	}

	Shader.Baked.Add(TextureSize, Baked);
	return FindTexture(Baked);
}

const FFolioRenderInterface::FTexture* FFolioRenderInterface::GetEffectTexture(
	const Rml::CompiledShaderHandle Handle)
{
	const TUniquePtr<FShader>* Found = Shaders.Find(Handle);
	if (!Found || !Found->IsValid())
	{
		return nullptr;
	}

	FShader& Shader = **Found;

	int Width = 1, Height = 1;
	Folio::SuggestShaderEffectSize(Shader.Effect, Shader.Dimensions.X, Shader.Dimensions.Y, Width, Height);
	const FIntPoint Size(Width, Height);

	if (const Rml::TextureHandle* Cached = Shader.Baked.Find(Size))
	{
		return FindTexture(*Cached);
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_BakeShaderEffect);
	SCOPE_CYCLE_COUNTER(STAT_FolioBakeDecorator);

	std::vector<unsigned char> Rgba;
	Folio::RasterizeShaderEffect(Shader.Effect, Width, Height, Rgba);

	// The rasteriser writes RGBA and Slate wants BGRA. Both are premultiplied already.
	TArray<uint8> Bgra;
	Bgra.SetNumUninitialized(int32(Rgba.size()));
	for (int32 Index = 0; Index + 3 < Bgra.Num(); Index += 4)
	{
		Bgra[Index + 0] = Rgba[std::size_t(Index) + 2];
		Bgra[Index + 1] = Rgba[std::size_t(Index) + 1];
		Bgra[Index + 2] = Rgba[std::size_t(Index) + 0];
		Bgra[Index + 3] = Rgba[std::size_t(Index) + 3];
	}

	const Rml::TextureHandle Texture = RegisterPixels(Bgra.GetData(), Size);
	if (!Texture)
	{
		return nullptr;
	}

	Shader.Baked.Add(Size, Texture);
	return FindTexture(Texture);
}

void FFolioRenderInterface::RenderShader(const Rml::CompiledShaderHandle Shader,
	const Rml::CompiledGeometryHandle GeometryHandle, const Rml::Vector2f Translation, Rml::TextureHandle)
{
	const TUniquePtr<FCompiledGeometry>* Found = Geometries.Find(GeometryHandle);
	if ((!ElementList && !DrawList && Layers.IsEmpty()) || !Found || !Found->IsValid()
		|| (*Found)->Vertices.IsEmpty())
	{
		return;
	}

	const FCompiledGeometry& Compiled = **Found;

	// A shader effect's geometry carries normalised coordinates rather than the gradient path's
	// pixel offsets, so the texture stretches over the box and needs no rescaling.
	if (const TUniquePtr<FShader>* Shading = Shaders.Find(Shader);
		Shading && Shading->IsValid() && (*Shading)->Effect.IsValid())
	{
		if (const FTexture* Entry = GetEffectTexture(Shader))
		{
			FVector2f Offset;
			const TArray<FSlateVertex>& Vertices = PositionedVertices(Compiled, Translation, Offset);

			if (DrawList)
			{
				DrawList->AddTriangles(Vertices, Compiled.Indices,
					static_cast<UTexture*>(Entry->Texture.Get()), Offset,
					/*bMasked*/ false, /*bMaskInverted*/ false);
				return;
			}

			ApplyClipping();
			DrawGeometry(Compiled, Translation, Entry->Handle, FVector2f(1.0f, 1.0f));
		}
		return;
	}

	// The gradient's parameters are expressed in the element box's pixel space, and so are
	// the geometry's texture coordinates. The box size is therefore the extent of those
	// coordinates, not of the positions, which differ once a border radius trims the corners.
	FVector2f Extent(Compiled.Vertices[0].TexCoords[0], Compiled.Vertices[0].TexCoords[1]);
	for (const FSlateVertex& Vertex : Compiled.Vertices)
	{
		Extent = FVector2f::Max(Extent, FVector2f(Vertex.TexCoords[0], Vertex.TexCoords[1]));
	}

	const FVector2f BoxSize(FMath::Max(Extent.X, 1.0f), FMath::Max(Extent.Y, 1.0f));

	const FTexture* Entry = GetGradientTexture(Shader, BoxSize);
	if (!Entry)
	{
		return;
	}

	if (DrawList || !Layers.IsEmpty())
	{
		FVector2f Offset;
		const TArray<FSlateVertex>& Vertices = PositionedVertices(Compiled, Translation, Offset);

		// The gradient's texture coordinates are pixel offsets in the element box, so they need scaling
		// into the 0..1 the recording's sampler expects. The Slate path does the same thing through its
		// UV scale argument.
		FFolioDraw Draw;
		Draw.Kind = FFolioDraw::EKind::Geometry;
		Draw.Texture.Reset(static_cast<UTexture*>(Entry->Texture.Get()));
		FFolioDrawList::Convert(Vertices, Compiled.Indices,
			DrawList ? Offset : Offset - LayerOrigin, /*bUnpremultiply*/ false,
			DrawList ? DrawList->ShouldDecodeSrgb() : true, Draw);

		for (FFolioUIVertex& Vertex : Draw.Vertices)
		{
			Vertex.UV /= BoxSize;
		}

		if (DrawList)
		{
			DrawList->GetDraws().Add(MoveTemp(Draw));
		}
		else
		{
			FlushCoveredFields(BoundsOf(Vertices, Offset));
			Layers.AddDraw(MoveTemp(Draw));
		}

		return;
	}

	ApplyClipping();
	DrawGeometry(Compiled, Translation, Entry->Handle, FVector2f(1.0f / BoxSize.X, 1.0f / BoxSize.Y));
}

void FFolioRenderInterface::ReleaseShader(const Rml::CompiledShaderHandle Shader)
{
	if (const TUniquePtr<FShader>* Found = Shaders.Find(Shader); Found && Found->IsValid())
	{
		for (const TPair<FIntPoint, Rml::TextureHandle>& Baked : (*Found)->Baked)
		{
			Textures.Remove(Baked.Value);
		}
	}

	Shaders.Remove(Shader);
}

//
// Distance-field atlases.
//

Rml::TextureHandle FFolioRenderInterface::LoadFieldAtlas(const Rml::String& Source,
	const TArray64<uint8>& Bgra, const FIntPoint Size)
{
	const std::string SidecarPath = Folio::MsdfSidecarPathFor(Source);
	if (SidecarPath.empty())
	{
		return 0;
	}

	Rml::FileInterface* Files = Rml::GetFileInterface();
	const Rml::FileHandle File = Files ? Files->Open(SidecarPath) : Rml::FileHandle();
	if (!File)
	{
		// The suffix is a promise the bake makes, so a missing sidecar is a broken pair rather
		// than ordinary art that happens to be named this way.
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' is named as a distance-field atlas but has no sidecar beside it; drawn as ordinary art"),
			*Folio::ToFString(Source));
		return 0;
	}

	const size_t Length = Files->Length(File);
	std::string Text(Length, '\0');
	Files->Read(Text.data(), Length, File);
	Files->Close(File);

	Folio::FMsdfSidecar Sidecar;
	if (!Folio::ParseMsdfSidecar(Text, Sidecar) || !Sidecar.IsValid())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("the sidecar beside '%s' is unreadable; drawn as ordinary art"), *Folio::ToFString(Source));
		return 0;
	}

	// Through the same sink the font engine builds its pages with, so a baked sheet and a glyph
	// atlas are the same kind of resource from here on and resolve through the same material.
	const Folio::FMsdfHandle Page = CreateAtlasPage(Folio::FMsdfPoint{ Size.X, Size.Y }, Sidecar.Params);
	if (Page == Folio::InvalidMsdfHandle)
	{
		return 0;
	}

	// The sink takes RGBA and the decode gave BGRA, so the channels are swapped on the way in
	// rather than leaving the field's three distance channels in the wrong order.
	TArray64<uint8> Rgba;
	Rgba.SetNumUninitialized(Bgra.Num());
	for (int64 Index = 0; Index + 3 < Bgra.Num(); Index += 4)
	{
		Rgba[Index + 0] = Bgra[Index + 2];
		Rgba[Index + 1] = Bgra[Index + 1];
		Rgba[Index + 2] = Bgra[Index + 0];
		Rgba[Index + 3] = Bgra[Index + 3];
	}

	UploadAtlasRegion(Page, Folio::FMsdfRect{ 0, 0, Size.X, Size.Y }, Rgba.GetData(), Size.X * 4);

	FieldAtlasesFromFile.Add(Rml::TextureHandle(Page));
	return Rml::TextureHandle(Page);
}

Folio::FMsdfHandle FFolioRenderInterface::CreateAtlasPage(const Folio::FMsdfPoint Size,
	const Folio::FMsdfAtlasParams& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_CreateAtlasPage);
	LLM_SCOPE_BYTAG(Folio);

	if (Size.X <= 0 || Size.Y <= 0)
	{
		return Folio::InvalidMsdfHandle;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8);
	if (!Texture)
	{
		return Folio::InvalidMsdfHandle;
	}

	// Emphatically not sRGB. These channels are distances, not colour, and decoding them as
	// colour moves every edge. Bilinear because reconstructing an edge depends on interpolating
	// between distances, clamped because a field means nothing outside its own rectangle, and
	// unstreamed because glyphs are written into it as they are first drawn.
	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->NeverStream = true;

	// Cleared rather than left as whatever the allocator held: every texel is a distance, and
	// garbage in an unwritten one reads as ink.
	{
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memzero(Data, Mip.BulkData.GetBulkDataSize());
		Mip.BulkData.Unlock();
	}
	Texture->UpdateResource();

	TSharedPtr<FMsdfPage> Page = MakeShared<FMsdfPage>();
	Page->Texture = TStrongObjectPtr<UTexture2D>(Texture);
	Page->Params = Params;

	TUniquePtr<FMsdfBinding> Binding = MakeUnique<FMsdfBinding>();
	Binding->Page = Page;

	const Rml::TextureHandle Handle = NextHandle++;
	MsdfBindings.Add(Handle, MoveTemp(Binding));
	return Folio::FMsdfHandle(Handle);
}

void FFolioRenderInterface::UploadAtlasRegion(const Folio::FMsdfHandle Page, const Folio::FMsdfRect Region,
	const unsigned char* Rgba, const int Stride)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_UploadAtlasRegion);

	const TUniquePtr<FMsdfBinding>* Found = MsdfBindings.Find(Rml::TextureHandle(Page));
	if (!Found || !Found->IsValid() || !(*Found)->Page.IsValid() || Rgba == nullptr)
	{
		return;
	}

	UTexture2D* Texture = (*Found)->Page->Texture.Get();
	if (!Texture || Region.Width <= 0 || Region.Height <= 0)
	{
		return;
	}

	// The field arrives RGBA and the texture is BGRA. The three colour channels are
	// interchangeable to the shader, since a median does not care which is which, but alpha
	// carries the true distance and has to stay where it is.
	const int32 Width = Region.Width;
	const int32 Height = Region.Height;
	const int32 ByteCount = Width * Height * 4;

	uint8* Bgra = static_cast<uint8*>(FMemory::Malloc(ByteCount));
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const unsigned char* SourceRow = Rgba + static_cast<int64>(Y) * Stride;
		uint8* DestRow = Bgra + static_cast<int64>(Y) * Width * 4;
		for (int32 X = 0; X < Width; ++X)
		{
			DestRow[X * 4 + 0] = SourceRow[X * 4 + 2];
			DestRow[X * 4 + 1] = SourceRow[X * 4 + 1];
			DestRow[X * 4 + 2] = SourceRow[X * 4 + 0];
			DestRow[X * 4 + 3] = SourceRow[X * 4 + 3];
		}
	}

	// Written into the texture's own mip as well as uploaded, because UpdateTextureRegions touches
	// the RHI texture and nothing else. Anything that reinitialises the resource rebuilds it from
	// this mip: switching the editor to a mobile preview platform does, and so does a device
	// profile change, and every glyph written so far would otherwise come back blank while the
	// boxes around them kept drawing. The mip is already allocated, so mirroring costs nothing.
	if (FTexturePlatformData* Platform = Texture->GetPlatformData(); Platform && Platform->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = Platform->Mips[0];
		const int64 PageWidth = Mip.SizeX;
		const int64 PageHeight = Mip.SizeY;

		if (Region.X >= 0 && Region.Y >= 0 && Region.X + Width <= PageWidth && Region.Y + Height <= PageHeight
			&& Mip.BulkData.GetBulkDataSize() >= PageWidth * PageHeight * 4)
		{
			if (uint8* Mirror = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE)))
			{
				for (int32 Y = 0; Y < Height; ++Y)
				{
					FMemory::Memcpy(Mirror + ((Region.Y + Y) * PageWidth + Region.X) * 4,
						Bgra + static_cast<int64>(Y) * Width * 4, static_cast<SIZE_T>(Width) * 4);
				}
			}
			Mip.BulkData.Unlock();
		}
	}

	// A sub-rectangle update into the same texture, so every resource handle already handed out
	// stays valid. Reallocating would strand geometry RmlUi has cached against this page.
	FUpdateTextureRegion2D* UpdateRegion =
		new FUpdateTextureRegion2D(Region.X, Region.Y, 0, 0, Width, Height);

	Texture->UpdateTextureRegions(0, 1, UpdateRegion, Width * 4, 4, Bgra,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
}

Folio::FMsdfHandle FFolioRenderInterface::CreateEffectAlias(const Folio::FMsdfHandle Page,
	const Folio::FMsdfEffectParams& Effect)
{
	const TUniquePtr<FMsdfBinding>* Source = MsdfBindings.Find(Rml::TextureHandle(Page));
	if (!Source || !Source->IsValid())
	{
		return Folio::InvalidMsdfHandle;
	}

	TUniquePtr<FMsdfBinding> Alias = MakeUnique<FMsdfBinding>();
	Alias->Page = (*Source)->Page;
	Alias->Effect = Effect;

	const Rml::TextureHandle Handle = NextHandle++;
	MsdfBindings.Add(Handle, MoveTemp(Alias));
	return Folio::FMsdfHandle(Handle);
}

void FFolioRenderInterface::ReleaseAtlasHandle(const Folio::FMsdfHandle Handle)
{
	// Aliases hold the page by shared pointer, so dropping one leaves the texture alive as long
	// as anything else still names it.
	MsdfBindings.Remove(Rml::TextureHandle(Handle));
}

FFolioRenderInterface::FMsdfBinding* FFolioRenderInterface::FindMsdfBinding(const Rml::TextureHandle Handle)
{
	const TUniquePtr<FMsdfBinding>* Found = MsdfBindings.Find(Handle);
	if (!Found || !Found->IsValid())
	{
		return nullptr;
	}

	FMsdfBinding& Binding = **Found;
	return Binding.Page.IsValid() && Binding.Page->Texture.IsValid() ? &Binding : nullptr;
}

void FFolioRenderInterface::PackMsdfParameters(const FMsdfBinding& Binding, FVector4f& OutField,
	FVector4f& OutEffect)
{
	const UTexture2D* Texture = Binding.Page->Texture.Get();
	const float Range = static_cast<float>(Binding.Page->Params.PxRange);

	// Unhalved: FolioMsdf.ush applies the half itself. Halving it here as well doubles the
	// coverage ramp and text goes soft.
	OutField = FVector4f(
		Range / static_cast<float>(Texture->GetSizeX()),
		Range / static_cast<float>(Texture->GetSizeY()),
		0.5f,
		1.0f);

	OutEffect = FVector4f(
		static_cast<float>(static_cast<int32>(Binding.Effect.Effect)),
		Binding.Effect.OffsetEm,
		Binding.Effect.SoftnessEm,
		0.0f);
}

FFolioRenderInterface::FMsdfBinding* FFolioRenderInterface::ResolveMsdfBinding(const Rml::TextureHandle Handle)
{
	FMsdfBinding* Existing = FindMsdfBinding(Handle);
	if (!Existing)
	{
		return nullptr;
	}

	FMsdfBinding& Binding = *Existing;
	if (Binding.Handle.IsValid())
	{
		return &Binding;
	}

	if (!bMsdfMaterialChecked)
	{
		bMsdfMaterialChecked = true;
		MsdfMaterial = TStrongObjectPtr<UMaterialInterface>(
			LoadObject<UMaterialInterface>(nullptr, TEXT("/Folio/M_FolioMSDF.M_FolioMSDF")));

		if (!MsdfMaterial.IsValid())
		{
			UE_LOG(LogFolio, Error,
				TEXT("could not load /Folio/M_FolioMSDF; text has no shader to resolve its fields"));
		}
	}

	if (!MsdfMaterial.IsValid() || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(MsdfMaterial.Get(), nullptr);
	if (!Instance)
	{
		return nullptr;
	}

	const UTexture2D* Texture = Binding.Page->Texture.Get();
	const FVector2f PageSize(static_cast<float>(Texture->GetSizeX()), static_cast<float>(Texture->GetSizeY()));

	FVector4f Field, Effect;
	PackMsdfParameters(Binding, Field, Effect);

	Instance->SetTextureParameterValue(TEXT("Atlas"), Binding.Page->Texture.Get());
	Instance->SetScalarParameterValue(TEXT("UnitRangeX"), Field.X);
	Instance->SetScalarParameterValue(TEXT("UnitRangeY"), Field.Y);
	Instance->SetScalarParameterValue(TEXT("Threshold"), Field.Z);
	Instance->SetScalarParameterValue(TEXT("EffectMode"), Effect.X);
	Instance->SetScalarParameterValue(TEXT("EffectOffset"), Effect.Y);
	Instance->SetScalarParameterValue(TEXT("EffectSoftness"), Effect.Z);

	// Textured text. The pattern is a file rather than a sprite, because a font effect is built
	// while the stylesheet parses and has no route to a spritesheet.
	if (UTexture* Fill = ResolveFillTexture(Binding.Effect.FillImage))
	{
		Instance->SetTextureParameterValue(TEXT("Fill"), Fill);
		Instance->SetScalarParameterValue(TEXT("FillSize"), FMath::Max(Binding.Effect.FillSizePx, 1.0f));
		Instance->SetScalarParameterValue(TEXT("FillStrength"), 1.0f);
	}
	else
	{
		Instance->SetScalarParameterValue(TEXT("FillStrength"), 0.0f);
	}

	Binding.Material = TStrongObjectPtr<UMaterialInstanceDynamic>(Instance);
	Binding.Brush.SetResourceObject(Instance);
	Binding.Brush.ImageSize = PageSize;

	// A material resource is the whole mechanism: MakeCustomVerts tags its batches
	// ESlateShader::Custom, and Slate answers that with TSlateMaterialShaderPS<Custom>, so the
	// material supplies the pixel shader for geometry that is otherwise drawn exactly as before.
	Binding.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Binding.Brush);

	return Binding.Handle.IsValid() ? &Binding : nullptr;
}

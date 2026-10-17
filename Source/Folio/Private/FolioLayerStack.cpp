#include "FolioLayerStack.h"

#include "FolioColourShader.h"
#include "FolioMaskShader.h"
#include "FolioResolveShader.h"

#include "FolioInsights.h"

#include "CommonRenderResources.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"
#include "Hash/CityHash.h"
#include "FolioBlurShader.h"
#include "FolioModule.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
	/** The value written into the stencil where a mask covers. */
	constexpr uint32 MaskStencilRef = 1;

	TAutoConsoleVariable<bool> CVarLayerCache(
		TEXT("folio.Layers.Cache"), true,
		TEXT("Reuse a layer's finished picture when what it draws has not changed since an earlier frame, so ")
		TEXT("only layers whose content moved are drawn again. Off draws every layer every frame."));

	TAutoConsoleVariable<bool> CVarLayerTighten(
		TEXT("folio.Layers.Tighten"), true,
		TEXT("Size each layer to what it actually draws rather than to the region it was pushed for, which ")
		TEXT("is the whole screen for an element with no clip. Off allocates the full region."));

	/** Shrunk only when the drawn area is under this share of the layer, so a near-full layer keeps its pooled size. */
	constexpr float TightenBelow = 0.6f;

	/** Frames a cached layer survives unused, so a document that skips a paint does not lose its cache. */
	constexpr uint64 CacheFrames = 2;

	template <typename T>
	uint64 HashBytes(const TArray<T>& Items, const uint64 Seed)
	{
		return Items.IsEmpty() ? Seed : CityHash64WithSeed(reinterpret_cast<const char*>(Items.GetData()),
			Items.Num() * sizeof(T), Seed);
	}

	template <typename T>
	uint64 HashValue(const T& Value, const uint64 Seed)
	{
		return CityHash64WithSeed(reinterpret_cast<const char*>(&Value), sizeof(T), Seed);
	}

	/** Frames left to trace cache misses for; folio.Layers.CacheTrace sets it. */
	int32 TraceFramesLeft = 0;

	FAutoConsoleCommand CacheTraceCommand(
		TEXT("folio.Layers.CacheTrace"),
		TEXT("folio.Layers.CacheTrace [frames]. Logs every layer drawn rather than reused from the cache over the ")
		TEXT("next frames (default 1), with what changed since the layer in the same place the frame before: its ")
		TEXT("place, filters, mask, or which draw and which part of it. The way to find what keeps a layer from caching."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TraceFramesLeft = Args.IsEmpty() ? 1 : FMath::Max(1, FCString::Atoi(*Args[0]));
		}));

	template <typename T, typename F>
	uint64 HashField(const TArray<T>& Items, F Field)
	{
		uint64 Hash = 0;
		for (const T& Item : Items)
		{
			Hash = HashValue(Field(Item), Hash);
		}
		return Hash;
	}
}

FFolioLayerStack::FFolioLayerStack() = default;

FFolioLayerStack::~FFolioLayerStack()
{
	Reset();
}

void FFolioLayerStack::Reset()
{
	for (const TPair<FIntPoint, TStrongObjectPtr<UTextureRenderTarget2D>>& Pooled : Pool)
	{
		DEC_MEMORY_STAT_BY(STAT_FolioLayerMemory, int64(Pooled.Key.X) * int64(Pooled.Key.Y) * 4);
	}

	Stack.Empty();
	Pool.Empty();
	Cache.Empty();
	Cached.Empty();
	PrintsOfResults.Empty();
	ResultRects.Empty();
	Scale = 1;
}

FFolioLayerStack::FCacheStats FFolioLayerStack::TakeCacheStats()
{
	const FCacheStats Taken = Stats;
	Stats = FCacheStats();
	return Taken;
}

bool FFolioLayerStack::PrintOf(const FLayer& Layer, const float BlurSigma,
	const TArrayView<const Folio::FColourFilter> Colour, UTextureRenderTarget2D* Mask, uint64& OutPrint)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_PrintLayer);

	if (!CVarLayerCache.GetValueOnGameThread())
	{
		return false;
	}

	BeginTraceFrame();
	FTracedLayer* Traced = TraceFramesLeft > 0 ? &TracedNow.AddDefaulted_GetRef() : nullptr;
	if (Traced)
	{
		Traced->Size = Layer.Size;
		Traced->Origin = Layer.Origin;
	}

	uint64 Print = HashValue(Layer.Size, 0x464f4c494f);
	Print = HashValue(Layer.ClipSize, Print);
	Print = HashValue(Layer.Origin, Print);
	const uint64 Place = Print;

	Print = HashValue(BlurSigma, Print);
	for (const Folio::FColourFilter& Filter : Colour)
	{
		Print = HashValue(Filter.Kind, Print);
		Print = HashValue(Filter.Amount, Print);
	}
	if (Traced)
	{
		Traced->Place = Place;
		Traced->Filters = Print;
	}

	if (Mask)
	{
		const uint64* MaskPrint = PrintsOfResults.Find(Mask);
		if (!MaskPrint)
		{
			if (Traced)
			{
				Traced->Uncacheable = TEXT("its mask is a layer that was not cached");
			}
			return false;
		}
		Print = HashValue(*MaskPrint, Print);
		if (Traced)
		{
			Traced->Mask = *MaskPrint;
		}
	}

	for (const FFolioDraw& Draw : Layer.Draws.GetDraws())
	{
		// A finished layer is printed by what it shows rather than by which target holds it this frame. A
		// target nobody printed is the game's, and can change without the recording changing at all.
		UTexture* Texture = Draw.Texture.Get();
		uint64 TextureKey = 0;
		if (UTextureRenderTarget2D* Target = Cast<UTextureRenderTarget2D>(Texture))
		{
			const uint64* Shown = PrintsOfResults.Find(Target);
			if (!Shown)
			{
				if (Traced)
				{
					Traced->Uncacheable = FString::Printf(TEXT("it samples render target %s, which the game draws"), *Target->GetName());
				}
				return false;
			}
			TextureKey = *Shown;
		}
		else if (Texture && !Texture->IsA<UTexture2D>())
		{
			if (Traced)
			{
				Traced->Uncacheable = FString::Printf(TEXT("it samples %s, a %s that can change behind Folio"), *Texture->GetName(), *Texture->GetClass()->GetName());
			}
			return false;
		}
		else
		{
			TextureKey = reinterpret_cast<UPTRINT>(Texture);
		}
		Print = HashValue(TextureKey, Print);

		uint64 State = HashValue(Draw.Kind, 0);
		State = HashValue(Draw.bMasked, State);
		State = HashValue(Draw.bMaskInverted, State);
		State = HashValue(Draw.bTiles, State);
		State = HashValue(Draw.bStraightAlpha, State);
		State = HashValue(Draw.Field, State);
		State = HashValue(Draw.Effect, State);
		State = HashValue(Draw.Scissor, State);
		Print = HashValue(State, Print);
		Print = HashBytes(Draw.Vertices, Print);
		Print = HashBytes(Draw.Indices, Print);

		if (Traced)
		{
			FTracedDraw& Part = Traced->Draws.AddDefaulted_GetRef();
			Part.Texture = TextureKey;
			Part.State = State;
			Part.Positions = HashField(Draw.Vertices, [](const FFolioUIVertex& V) { return V.Position; });
			Part.UVs = HashField(Draw.Vertices, [](const FFolioUIVertex& V) { return V.UV; });
			Part.Colours = HashField(Draw.Vertices, [](const FFolioUIVertex& V) { return V.Color; });
			Part.Indices = HashBytes(Draw.Indices, 0);
			Part.TextureName = Texture ? Texture->GetName() : TEXT("no texture");
		}
	}

	OutPrint = Print;
	return true;
}

void FFolioLayerStack::BeginTraceFrame()
{
	if (TracedFrame == GFrameCounter)
	{
		return;
	}

	// Layers are compared by paint order, which holds for as long as the document's structure does.
	if (!TracedNow.IsEmpty())
	{
		TraceFramesLeft = FMath::Max(0, TraceFramesLeft - 1);
	}
	TracedFrame = GFrameCounter;
	TracedBefore = MoveTemp(TracedNow);
	TracedNow.Reset();
}

void FFolioLayerStack::TraceMiss(const FTracedLayer& Now, const int32 Slot) const
{
	const FString Where = FString::Printf(TEXT("layer %d (%dx%d at %d,%d, %d draws)"), Slot, Now.Size.X, Now.Size.Y,
		Now.Origin.X, Now.Origin.Y, Now.Draws.Num());

	if (!Now.Uncacheable.IsEmpty())
	{
		UE_LOG(LogFolio, Display, TEXT("folio cache: %s cannot cache: %s"), *Where, *Now.Uncacheable);
		return;
	}

	if (!TracedBefore.IsValidIndex(Slot))
	{
		UE_LOG(LogFolio, Display, TEXT("folio cache: %s is new this frame"), *Where);
		return;
	}

	const FTracedLayer& Before = TracedBefore[Slot];
	TArray<FString> Changes;
	if (Before.Place != Now.Place)
	{
		Changes.Add(FString::Printf(TEXT("place (was %dx%d at %d,%d)"), Before.Size.X, Before.Size.Y, Before.Origin.X, Before.Origin.Y));
	}
	else if (Before.Filters != Now.Filters)
	{
		Changes.Add(TEXT("filters (opacity, blur or colour)"));
	}
	if (Before.Mask != Now.Mask)
	{
		Changes.Add(TEXT("mask"));
	}
	if (Before.Draws.Num() != Now.Draws.Num())
	{
		Changes.Add(FString::Printf(TEXT("draw count (was %d)"), Before.Draws.Num()));
	}

	for (int32 Index = 0; Index < FMath::Min(Before.Draws.Num(), Now.Draws.Num()); ++Index)
	{
		const FTracedDraw& A = Before.Draws[Index];
		const FTracedDraw& B = Now.Draws[Index];
		TArray<FString> Parts;
		if (A.Texture != B.Texture) { Parts.Add(TEXT("texture")); }
		if (A.State != B.State) { Parts.Add(TEXT("state or scissor")); }
		if (A.Positions != B.Positions) { Parts.Add(TEXT("positions")); }
		if (A.UVs != B.UVs) { Parts.Add(TEXT("uvs")); }
		if (A.Colours != B.Colours) { Parts.Add(TEXT("colours")); }
		if (A.Indices != B.Indices) { Parts.Add(TEXT("indices")); }
		if (!Parts.IsEmpty())
		{
			Changes.Add(FString::Printf(TEXT("draw %d (%s): %s"), Index, *B.TextureName, *FString::Join(Parts, TEXT(", "))));
		}
	}

	UE_LOG(LogFolio, Display, TEXT("folio cache: %s changed: %s"), *Where,
		Changes.IsEmpty() ? TEXT("nothing visible, so its cached result had been dropped") : *FString::Join(Changes, TEXT("; ")));
}

UTextureRenderTarget2D* FFolioLayerStack::FindCached(const uint64 Print)
{
	TrimCache();

	FCached* Found = Cache.Find(Print);
	if (!Found || !Found->Result.IsValid())
	{
		return nullptr;
	}

	Found->LastUsedFrame = GFrameCounter;
	return Found->Result.Get();
}

void FFolioLayerStack::AddCached(const uint64 Print, UTextureRenderTarget2D* Result, const bool bPooled)
{
	if (!Result)
	{
		return;
	}

	FCached& Entry = Cache.FindOrAdd(Print);
	if (Entry.Result.IsValid() && Entry.Result.Get() != Result)
	{
		Cached.Remove(Entry.Result.Get());
		if (Entry.bPooled)
		{
			Release(Entry.Result.Get());
		}
	}

	Entry.Result = TStrongObjectPtr<UTextureRenderTarget2D>(Result);
	Entry.LastUsedFrame = GFrameCounter;
	Entry.bPooled = bPooled;
	Cached.Add(Result);
}

void FFolioLayerStack::TrimCache()
{
	if (TrimmedFrame == GFrameCounter)
	{
		return;
	}
	TrimmedFrame = GFrameCounter;

	// The prints of results are this frame's: a target from the pool is a different picture next time.
	PrintsOfResults.Reset();
	for (const TPair<uint64, FCached>& Entry : Cache)
	{
		if (Entry.Value.Result.IsValid())
		{
			PrintsOfResults.Add(Entry.Value.Result.Get(), Entry.Key);
		}
	}

	for (auto It = Cache.CreateIterator(); It; ++It)
	{
		if (GFrameCounter - It->Value.LastUsedFrame <= CacheFrames)
		{
			continue;
		}

		UTextureRenderTarget2D* Result = It->Value.Result.Get();
		Cached.Remove(Result);
		PrintsOfResults.Remove(Result);
		if (It->Value.bPooled)
		{
			Release(Result);
		}
		It.RemoveCurrent();
	}
}

void FFolioLayerStack::Discard(FLayer& Layer)
{
	Layer.Draws.Reset();
	for (const TStrongObjectPtr<UTextureRenderTarget2D>& Target : Layer.Sampled)
	{
		Release(Target.Get());
	}
	Layer.Sampled.Reset();
}

FIntPoint FFolioLayerStack::DocumentSizeOf(const FLayer& Layer)
{
	return Layer.ClipSize.X > 0 && Layer.ClipSize.Y > 0 ? Layer.ClipSize : Layer.Size;
}

float FFolioLayerStack::ScaleOf(const FLayer& Layer)
{
	const FIntPoint Document = DocumentSizeOf(Layer);
	return Document.X > 0 ? float(Layer.Size.X) / float(Document.X) : 1.0f;
}

FSlateRect FFolioLayerStack::CurrentContentRect() const
{
	if (Stack.IsEmpty())
	{
		return FSlateRect();
	}

	const FLayer& Layer = Stack.Last();

	bool bAny = false;
	FVector2f Min = FVector2f::ZeroVector;
	FVector2f Max = FVector2f::ZeroVector;

	for (const FFolioDraw& Draw : Layer.Draws.GetDraws())
	{
		// A mask contributes no colour, so it is not part of what the layer paints over anything.
		if (Draw.Kind != FFolioDraw::EKind::Geometry && Draw.Kind != FFolioDraw::EKind::Field)
		{
			continue;
		}

		for (const FFolioUIVertex& Vertex : Draw.Vertices)
		{
			Min = bAny ? FVector2f::Min(Min, Vertex.Position) : Vertex.Position;
			Max = bAny ? FVector2f::Max(Max, Vertex.Position) : Vertex.Position;
			bAny = true;
		}
	}

	if (!bAny)
	{
		return FSlateRect();
	}

	// Recorded against the layer's origin, and the caller works in the document's space.
	const FVector2f Origin(float(Layer.Origin.X), float(Layer.Origin.Y));
	return FSlateRect(Min.X + Origin.X, Min.Y + Origin.Y, Max.X + Origin.X, Max.Y + Origin.Y);
}

FIntRect FFolioLayerStack::DocumentRectOf(const FLayer& Layer)
{
	return FIntRect(Layer.Origin, Layer.Origin + DocumentSizeOf(Layer));
}

void FFolioLayerStack::Prepare(FLayer& Layer, const float Margin, const bool bMayShrink)
{
	if (Layer.Target.IsValid())
	{
		return;
	}

	const FIntPoint Document = DocumentSizeOf(Layer);
	const int32 LayerScale = FMath::Max(1, FMath::RoundToInt(ScaleOf(Layer)));

	if (bMayShrink && CVarLayerTighten.GetValueOnGameThread() && Document.X > 0 && Document.Y > 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Folio_TightenLayer);

		bool bAny = false;
		FVector2f Min = FVector2f::ZeroVector;
		FVector2f Max = FVector2f::ZeroVector;
		for (const FFolioDraw& Draw : Layer.Draws.GetDraws())
		{
			// Clip masks only limit what other draws reach, so they do not widen what the layer shows.
			if (Draw.Kind != FFolioDraw::EKind::Geometry && Draw.Kind != FFolioDraw::EKind::Field)
			{
				continue;
			}

			FVector2f DrawMin = FVector2f::ZeroVector;
			FVector2f DrawMax = FVector2f::ZeroVector;
			for (int32 Index = 0; Index < Draw.Vertices.Num(); ++Index)
			{
				const FVector2f Position = Draw.Vertices[Index].Position;
				DrawMin = Index == 0 ? Position : FVector2f::Min(DrawMin, Position);
				DrawMax = Index == 0 ? Position : FVector2f::Max(DrawMax, Position);
			}
			if (Draw.Vertices.IsEmpty())
			{
				continue;
			}

			if (Draw.Scissor.Area() > 0)
			{
				DrawMin = FVector2f::Max(DrawMin, FVector2f(Draw.Scissor.Min));
				DrawMax = FVector2f::Min(DrawMax, FVector2f(Draw.Scissor.Max));
				if (DrawMin.X >= DrawMax.X || DrawMin.Y >= DrawMax.Y)
				{
					continue;
				}
			}

			Min = bAny ? FVector2f::Min(Min, DrawMin) : DrawMin;
			Max = bAny ? FVector2f::Max(Max, DrawMax) : DrawMax;
			bAny = true;
		}

		// A layer that drew nothing still composites, so it keeps one transparent pixel rather than a screen of them.
		FIntRect Covered(0, 0, 1, 1);
		if (bAny)
		{
			Covered = FIntRect(
				FMath::FloorToInt(Min.X - Margin), FMath::FloorToInt(Min.Y - Margin),
				FMath::CeilToInt(Max.X + Margin), FMath::CeilToInt(Max.Y + Margin));
			Covered.Clip(FIntRect(FIntPoint::ZeroValue, Document));
		}

		if (Covered.Area() > 0 && Covered.Area() < TightenBelow * float(Document.X) * float(Document.Y))
		{
			const FVector2f Shift(Covered.Min);
			for (FFolioDraw& Draw : Layer.Draws.GetDraws())
			{
				for (FFolioUIVertex& Vertex : Draw.Vertices)
				{
					Vertex.Position -= Shift;
				}
				if (Draw.Scissor.Area() > 0)
				{
					Draw.Scissor -= Covered.Min;
				}
			}

			UE_LOG(LogFolio, VeryVerbose, TEXT("layer shrunk from %dx%d to %dx%d at %d,%d"), Document.X, Document.Y,
				Covered.Width(), Covered.Height(), Layer.Origin.X + Covered.Min.X, Layer.Origin.Y + Covered.Min.Y);

			Layer.Origin += Covered.Min;
			Layer.ClipSize = Covered.Size();
			Layer.Size = Covered.Size() * LayerScale;
		}
	}

	Layer.Target.Reset(Obtain(Layer.Size));
}

FVector4f FFolioLayerStack::MaskTransformFor(const FIntRect& Source, const float SourceScale,
	UTextureRenderTarget2D* Mask) const
{
	const FIntRect* MaskRect = Mask ? ResultRects.Find(Mask) : nullptr;
	if (!MaskRect || MaskRect->Width() <= 0 || MaskRect->Height() <= 0 || SourceScale <= 0.0f)
	{
		return FVector4f::Zero();
	}

	// Target pixel to document pixel, then document pixel to the mask's UV.
	const FVector2f MaskSize(MaskRect->Width(), MaskRect->Height());
	return FVector4f(
		1.0f / (SourceScale * MaskSize.X), 1.0f / (SourceScale * MaskSize.Y),
		float(Source.Min.X - MaskRect->Min.X) / MaskSize.X, float(Source.Min.Y - MaskRect->Min.Y) / MaskSize.Y);
}

void FFolioLayerStack::TakeRectOf(FLayer& Layer, UTextureRenderTarget2D* Result) const
{
	const FIntRect* Rect = ResultRects.Find(Result);
	if (!Rect)
	{
		return;
	}

	const int32 LayerScale = FMath::Max(1, FMath::RoundToInt(ScaleOf(Layer)));
	Layer.Origin = Rect->Min;
	Layer.ClipSize = Rect->Size();
	Layer.Size = Rect->Size() * LayerScale;
}

UTextureRenderTarget2D* FFolioLayerStack::Obtain(const FIntPoint Size)
{
	TArray<TStrongObjectPtr<UTextureRenderTarget2D>> Available;
	Pool.MultiFind(Size, Available);

	if (Available.Num() > 0 && Available[0].IsValid())
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Found = Available[0];
		Pool.RemoveSingle(Size, Found);
		Found->UpdateResourceImmediate(true);

		return Found.Get();
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_AllocateLayer);
	SCOPE_CYCLE_COUNTER(STAT_FolioAllocateTarget);

	// Four bytes a texel, and a sample count multiplies the texels by itself. Reported because that
	// is the number that decides whether a sample count is affordable, and nothing else shows it.
	INC_MEMORY_STAT_BY(STAT_FolioLayerMemory, int64(Size.X) * int64(Size.Y) * 4);

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
	Target->RenderTargetFormat = RTF_RGBA8;

	// Premultiplied transparent, not opaque black: a layer that starts black fringes every
	// edge it later composites over.
	Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	Target->bAutoGenerateMips = false;
	Target->bForceLinearGamma = false;
	Target->InitAutoFormat(Size.X, Size.Y);
	Target->UpdateResourceImmediate(true);

	return Target;
}

void FFolioLayerStack::Release(UTextureRenderTarget2D* Target)
{
	// A cached result is still somebody's picture; the cache returns it to the pool when it drops it.
	if (Target && !Cached.Contains(Target))
	{
		Pool.Add(FIntPoint(Target->SizeX, Target->SizeY), TStrongObjectPtr<UTextureRenderTarget2D>(Target));
	}
}

bool FFolioLayerStack::Push(const FIntPoint Size, const FIntPoint Origin)
{
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	FLayer& Layer = Stack.AddDefaulted_GetRef();
	Layer.Size = FIntPoint(Size.X * Scale, Size.Y * Scale);
	Layer.Origin = Origin;
	Layer.ClipSize = Size;
	return true;
}

bool FFolioLayerStack::HasDocumentLayer() const
{
	for (const FLayer& Layer : Stack)
	{
		if (!Layer.bCallerOwned)
		{
			return true;
		}
	}

	return false;
}

bool FFolioLayerStack::PushMatchingTop()
{
	if (Stack.IsEmpty())
	{
		return false;
	}

	// Copied before acquiring, which appends to the stack and would leave a reference dangling.
	const FIntPoint Size = Stack.Last().Size;
	const FIntPoint Clip = Stack.Last().ClipSize;
	const FIntPoint Origin = Stack.Last().Origin;

	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	// Clip is carried too, or a layer over a supersampled surface would replay the same geometry at the
	// target's scale instead of the document's.
	FLayer& Layer = Stack.AddDefaulted_GetRef();
	Layer.Size = Size;
	Layer.ClipSize = Clip;
	Layer.Origin = Origin;
	return true;
}

bool FFolioLayerStack::PushInto(UTextureRenderTarget2D* Target, const FIntPoint ClipSize)
{
	if (!Target || Target->SizeX <= 0 || Target->SizeY <= 0)
	{
		return false;
	}

	FLayer& Layer = Stack.AddDefaulted_GetRef();
	Layer.Target = TStrongObjectPtr<UTextureRenderTarget2D>(Target);
	Layer.Size = FIntPoint(Target->SizeX, Target->SizeY);
	Layer.ClipSize = ClipSize;
	Layer.bCallerOwned = true;
	return true;
}

void FFolioLayerStack::Pop()
{
	if (Stack.IsEmpty())
	{
		return;
	}

	FLayer Layer = Stack.Pop();
	ResolveAndRecycle(Layer);

	// A handed-off target belongs to the layer sampling it, which releases it after its own resolve.
	if (!Layer.bCallerOwned && !Layer.bHandedOff)
	{
		Release(Layer.Target.Get());
	}
}

void FFolioLayerStack::AddDraw(FFolioDraw&& Draw)
{
	if (Stack.IsEmpty() || Draw.Indices.Num() < 3)
	{
		return;
	}

	FLayer& Layer = Stack.Last();
	Draw.bMasked = Layer.bClipMaskEnabled;
	Draw.bMaskInverted = Layer.bClipMaskInverted;
	Layer.Draws.GetDraws().Add(MoveTemp(Draw));
}

void FFolioLayerStack::DrawTriangles(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset, const bool bTiles, const bool bStraightAlpha)
{
	if (Stack.IsEmpty() || Indices.Num() < 3 || !Texture)
	{
		return;
	}

	FLayer& Layer = Stack.Last();
	Layer.Draws.AddTriangles(Vertices, Indices, Texture, Offset,
		Layer.bClipMaskEnabled, Layer.bClipMaskInverted, bTiles, bStraightAlpha);
}

void FFolioLayerStack::DrawField(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset, const FVector4f Field, const FVector4f Effect)
{
	if (Stack.IsEmpty() || Indices.Num() < 3 || !Texture)
	{
		return;
	}

	FLayer& Layer = Stack.Last();
	Layer.Draws.AddField(Vertices, Indices, Texture, Offset, Field, Effect,
		Layer.bClipMaskEnabled, Layer.bClipMaskInverted);
}

void FFolioLayerStack::DrawFieldClipped(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset, const FVector4f Field, const FVector4f Effect,
	const FIntRect& Scissor, const int32 DepthFromTop)
{
	const int32 Index = Stack.Num() - 1 - DepthFromTop;
	if (!Stack.IsValidIndex(Index) || Indices.Num() < 3 || !Texture)
	{
		return;
	}

	FLayer& Layer = Stack[Index];
	Layer.Draws.AddFieldAt(Vertices, Indices, Texture, Offset, Field, Effect,
		/*bMasked*/ false, /*bMaskInverted*/ false, Scissor);
}

void FFolioLayerStack::SetClipMask(const Rml::ClipMaskOperation Operation, const TArray<FSlateVertex>& Vertices,
	const TArray<SlateIndex>& Indices, const FVector2f Offset)
{
	if (Stack.IsEmpty() || Indices.Num() < 3)
	{
		return;
	}

	FLayer& Layer = Stack.Last();

	// Which side of the stencil later draws test is the layer's state, not the mask's, so it is tracked
	// here rather than inside the recording.
	switch (Operation)
	{
	case Rml::ClipMaskOperation::Set:        Layer.bClipMaskInverted = false; break;
	case Rml::ClipMaskOperation::SetInverse: Layer.bClipMaskInverted = true; break;
	case Rml::ClipMaskOperation::Intersect:  break;
	}

	Layer.bClipMaskEnabled = true;
	Layer.Draws.AddClipMask(Operation, Vertices, Indices, Offset);
}

FFolioDrawList* FFolioLayerStack::CurrentDrawList()
{
	return Stack.IsEmpty() ? nullptr : &Stack.Last().Draws;
}

void FFolioLayerStack::EnableClipMask(const bool bEnable)
{
	if (!Stack.IsEmpty())
	{
		Stack.Last().bClipMaskEnabled = bEnable;
		if (!bEnable)
		{
			Stack.Last().bClipMaskInverted = false;
		}
	}
}

UTextureRenderTarget2D* FFolioLayerStack::DetachCurrent(const float BlurSigma,
	const TArrayView<const Folio::FColourFilter> Colour, UTextureRenderTarget2D* Mask, FIntPoint* OutOrigin)
{
	if (Stack.IsEmpty())
	{
		return nullptr;
	}

	FLayer& Layer = Stack.Last();

	uint64 Print = 0;
	const bool bPrinted = PrintOf(Layer, BlurSigma, Colour, Mask, Print);
	if (bPrinted)
	{
		// A caller that cannot be told where the result sits needs it covering the layer's whole region.
		UTextureRenderTarget2D* Reused = FindCached(Print);
		const FIntRect* ReusedRect = Reused ? ResultRects.Find(Reused) : nullptr;
		if (Reused && (OutOrigin || !ReusedRect || *ReusedRect == DocumentRectOf(Layer)))
		{
			Discard(Layer);
			PrintsOfResults.Add(Reused, Print);
			++Stats.Reused;
			INC_DWORD_STAT(STAT_FolioLayersReused);
			if (OutOrigin)
			{
				*OutOrigin = ReusedRect ? ReusedRect->Min : Layer.Origin;
			}
			return Reused;
		}
	}
	++Stats.Rendered;
	if (TraceFramesLeft > 0 && !TracedNow.IsEmpty())
	{
		TraceMiss(TracedNow.Last(), TracedNow.Num() - 1);
	}
	INC_DWORD_STAT(STAT_FolioLayersDrawn);

	// The layer goes on recording after this, in the region it was pushed for, so that is put back after.
	const FIntPoint PushedSize = Layer.Size;
	const FIntPoint PushedClip = Layer.ClipSize;
	const FIntPoint PushedOrigin = Layer.Origin;

	Prepare(Layer, BlurSigma > 0.0f ? FMath::CeilToFloat(3.0f * BlurSigma) + 2.0f : 2.0f, OutOrigin != nullptr);
	ResolveAndRecycle(Layer);

	Folio::ApplyColourFilters(Layer.Target.Get(), Colour);

	// Sigma arrives in the document's pixels, and a supersampled layer has more of them per document
	// pixel: an unscaled blur would tighten as the sample count rose. The step goes with it, so the
	// kernel reaches the same distance into the document rather than running out of taps sooner.
	if (BlurSigma > 0.0f)
	{
		const float LayerScale = ScaleOf(Layer);
		Folio::BlurRenderTarget(Layer.Target.Get(), BlurSigma * LayerScale, LayerScale);
	}

	// Last, because the mask decides what survives: masking before a blur would let the blur bleed
	// back over what the mask had just cut away.
	Folio::FMaskOptions MaskOptions;
	MaskOptions.MaskTransform = MaskTransformFor(DocumentRectOf(Layer), ScaleOf(Layer), Mask);
	Folio::ApplyMask(Layer.Target.Get(), Mask, MaskOptions);

	// The caller owns the detached target now and RmlUi keeps it indefinitely, so it never goes back to
	// the pool. The layer gets a new one only if it resolves again.
	UTextureRenderTarget2D* Detached = Layer.Target.Get();
	ResultRects.Add(Detached, DocumentRectOf(Layer));
	if (OutOrigin)
	{
		*OutOrigin = Layer.Origin;
	}

	Layer.Target.Reset();
	Layer.Size = PushedSize;
	Layer.ClipSize = PushedClip;
	Layer.Origin = PushedOrigin;

	if (bPrinted)
	{
		AddCached(Print, Detached, /*bPooled*/ false);
		PrintsOfResults.Add(Detached, Print);
	}
	return Detached;
}

void FFolioLayerStack::CompositeDown(const bool bBlend, const float BlurSigma,
	const TArrayView<const Folio::FColourFilter> Colour, UTextureRenderTarget2D* Mask)
{
	if (Stack.Num() < 2)
	{
		return;
	}

	FLayer& Source = Stack.Last();

	uint64 Print = 0;
	const bool bPrinted = PrintOf(Source, BlurSigma, Colour, Mask, Print);
	if (bPrinted)
	{
		if (UTextureRenderTarget2D* Reused = FindCached(Print))
		{
			Discard(Source);
			PrintsOfResults.Add(Reused, Print);
			++Stats.Reused;
			INC_DWORD_STAT(STAT_FolioLayersReused);
			TakeRectOf(Source, Reused);
			AddCompositeQuad(Source, Reused);
			return;
		}
	}
	++Stats.Rendered;
	if (TraceFramesLeft > 0 && !TracedNow.IsEmpty())
	{
		TraceMiss(TracedNow.Last(), TracedNow.Num() - 1);
	}
	INC_DWORD_STAT(STAT_FolioLayersDrawn);

	Prepare(Source, BlurSigma > 0.0f ? FMath::CeilToFloat(3.0f * BlurSigma) + 2.0f : 2.0f, /*bMayShrink*/ true);
	ResolveAndRecycle(Source);

	// Colour before blur, which is the order CSS gives and the order that matters: blurring a
	// desaturated layer and desaturating a blurred one differ wherever the blur crosses an edge.
	Folio::ApplyColourFilters(Source.Target.Get(), Colour);

	// A blur is a low pass, so the samples a supersampled layer holds are exactly what it is about to
	// throw away. Averaging down first makes the kernel cost what it costs at one sample per pixel -
	// at four samples each way that is sixteen times less work - and the composite magnifies the
	// result, which is a blurred image being blurred slightly differently.
	UTextureRenderTarget2D* Composited = Source.Target.Get();

	if (BlurSigma > 0.0f)
	{
		const int32 SourceScale = FMath::RoundToInt(ScaleOf(Source));
		const FIntPoint Document = DocumentSizeOf(Source);

		if (SourceScale > 1 && Document.X > 0 && Document.Y > 0)
		{
			if (UTextureRenderTarget2D* Small = Obtain(Document))
			{
				Folio::ResolveSupersample(Source.Target.Get(), Small, SourceScale);
				Composited = Small;
			}
		}

		// One sample per pixel once it is averaged down, and the layer's own density if there was
		// nothing to average into.
		const float BlurScale = Composited == Source.Target.Get() ? ScaleOf(Source) : 1.0f;
		Folio::BlurRenderTarget(Composited, BlurSigma * BlurScale, BlurScale);
	}

	Folio::FMaskOptions MaskOptions;
	MaskOptions.MaskTransform = MaskTransformFor(DocumentRectOf(Source),
		Composited == Source.Target.Get() ? ScaleOf(Source) : 1.0f, Mask);
	Folio::ApplyMask(Composited, Mask, MaskOptions);
	ResultRects.Add(Composited, DocumentRectOf(Source));

	// Kept before the quad is recorded, so the destination's release of what it sampled leaves it alone.
	if (bPrinted)
	{
		AddCached(Print, Composited, /*bPooled*/ true);
		PrintsOfResults.Add(Composited, Print);
		if (Composited == Source.Target.Get())
		{
			Source.bHandedOff = true;
		}
	}

	AddCompositeQuad(Source, Composited);
}

void FFolioLayerStack::AddCompositeQuad(FLayer& Source, UTextureRenderTarget2D* Composited)
{
	// Compositing is a full-target quad drawn into the layer below, recorded like any other
	// draw so it stays ordered with the geometry around it.
	FLayer& Destination = Stack[Stack.Num() - 2];

	FFolioDraw Draw;
	Draw.Kind = FFolioDraw::EKind::Geometry;
	Draw.Texture.Reset(Composited);
	Draw.bMasked = false;

	// The document's pixels, not the target's: this quad is recorded into the layer below, and a
	// recording is replayed through that layer's own document-to-target transform. Sized from the
	// target instead, a supersampled composite lands at the sample count squared.
	const FIntPoint SourceDocument = DocumentSizeOf(Source);
	const FVector2f Size(SourceDocument.X, SourceDocument.Y);
	const FLinearColor White(1.0f, 1.0f, 1.0f, 1.0f);

	// Where the source sits in the destination's space. The two match whenever a layer was pushed to
	// match the one below it, and differ when the source covers only its element's own region.
	const FVector2f At(float(Source.Origin.X - Destination.Origin.X),
		float(Source.Origin.Y - Destination.Origin.Y));

	Draw.Vertices.Add({ At,                                   FVector2f(0.0f, 0.0f), White });
	Draw.Vertices.Add({ At + FVector2f(Size.X, 0.0f),         FVector2f(1.0f, 0.0f), White });
	Draw.Vertices.Add({ At + FVector2f(Size.X, Size.Y),       FVector2f(1.0f, 1.0f), White });
	Draw.Vertices.Add({ At + FVector2f(0.0f, Size.Y),         FVector2f(0.0f, 1.0f), White });
	Draw.Indices = { 0, 1, 2, 0, 2, 3 };

	Destination.Draws.GetDraws().Add(MoveTemp(Draw));

	// The quad above samples this target, so it has to stay out of the pool until the destination has
	// resolved. Pop runs immediately after this and would otherwise recycle it.
	//
	// Which target that is depends on whether the blur averaged the layer down first: the small one is
	// what the quad reads, and the layer's own target is then free to go back on Pop as usual.
	if (Composited == Source.Target.Get())
	{
		Destination.Sampled.Add(Source.Target);
		Source.bHandedOff = true;
	}
	else
	{
		Destination.Sampled.Add(TStrongObjectPtr<UTextureRenderTarget2D>(Composited));
	}
}

void FFolioLayerStack::ResolveAndRecycle(FLayer& Layer)
{
	Resolve(Layer);

	for (const TStrongObjectPtr<UTextureRenderTarget2D>& Target : Layer.Sampled)
	{
		Release(Target.Get());
	}

	Layer.Sampled.Reset();
}

void FFolioLayerStack::Resolve(FLayer& Layer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_ResolveLayers);
	SCOPE_CYCLE_COUNTER(STAT_FolioResolveLayers);

	// A pooled layer is handed out cleared, so an empty one has nothing to do. A caller's target is not:
	// it lives between frames, and a document that draws nothing into it this frame - every glyph run
	// held back, and no background behind them - would otherwise composite the frame before.
	if (!Layer.Target.IsValid() || (Layer.Draws.IsEmpty() && !Layer.bCallerOwned))
	{
		Layer.Draws.Reset();
		return;
	}

	FTextureRenderTargetResource* Resource = Layer.Target->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		Layer.Draws.Reset();
		return;
	}

	TArray<FFolioDraw> Draws = MoveTemp(Layer.Draws.GetDraws());
	Layer.Draws.Reset();

	const FIntPoint Size = Layer.Size;

	// What the geometry was recorded in, which is the target's size unless a surface is drawing a
	// document at a higher resolution than it was laid out at.
	const FIntPoint Clip = Layer.ClipSize.X > 0 && Layer.ClipSize.Y > 0 ? Layer.ClipSize : Layer.Size;

	// A pooled layer is handed out already cleared, but a surface's target is the caller's and lives
	// between frames, so loading it keeps whatever the last render left. That is invisible while the
	// document repaints every pixel and obvious the moment it does not: a document that lays out
	// smaller, or that moves an absolutely positioned element, leaves the old image behind it.
	const bool bClear = Layer.bCallerOwned;

	// A clip mask is what the stencil is for, and most layers have none. Allocating one anyway costs a
	// depth-stencil the size of the target every time a layer resolves, which is a page of memory at
	// one sample per pixel and several at sixteen.
	bool bNeedsStencil = false;
	for (const FFolioDraw& Draw : Draws)
	{
		bNeedsStencil = bNeedsStencil
			|| Draw.bMasked
			|| (Draw.Kind != FFolioDraw::EKind::Geometry && Draw.Kind != FFolioDraw::EKind::Field);
	}

	ENQUEUE_RENDER_COMMAND(FolioResolveLayer)(
		[Resource, Size, Clip, bClear, bNeedsStencil, Draws = MoveTemp(Draws)]
		(FRHICommandListImmediate& RHICmdList) mutable
		{
			// The game thread's scope ended at the enqueue. Everything a layer actually costs to
			// replay is here, and without these it reports as nothing at all.
			TRACE_CPUPROFILER_EVENT_SCOPE(Folio_ReplayLayer);
			SCOPE_CYCLE_COUNTER(STAT_FolioLayerReplay);

			SCOPE_CYCLE_COUNTER(STAT_FolioLayerGraph);

			FRDGBuilder GraphBuilder(RHICmdList);
			// Braced: the scope has to close before Execute, which asserts on an open breadcrumb.
			{
				RDG_EVENT_SCOPE_STAT(GraphBuilder, FolioLayers, "FolioLayers");

				FRDGTextureRef Colour = RegisterExternalTexture(GraphBuilder,
					Resource->GetRenderTargetTexture(), TEXT("FolioLayerColour"));

				// The stencil only has to live as long as this layer's replay, which is exactly
				// why the draws are recorded and resolved together rather than one at a time.
				FRDGTextureRef Stencil = nullptr;
				if (bNeedsStencil)
				{
					const FRDGTextureDesc StencilDesc = FRDGTextureDesc::Create2D(Size, PF_DepthStencil,
						FClearValueBinding::DepthZero, TexCreate_DepthStencilTargetable);
					Stencil = GraphBuilder.CreateTexture(StencilDesc, TEXT("FolioLayerStencil"));
				}

				FFolioUIParameters* Parameters = GraphBuilder.AllocParameters<FFolioUIParameters>();
				Parameters->UISampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				Parameters->UITexture = GWhiteTexture->TextureRHI;

				// Document pixels to clip space. Y is flipped: RmlUi's origin is top left. Against the recorded
				// space rather than the target, so a larger target scales the same geometry up instead of
				// leaving it in a corner.
				Parameters->ClipTransform = FVector4f(2.0f / Clip.X, -2.0f / Clip.Y, -1.0f, 1.0f);
				Parameters->RenderTargets[0] = FRenderTargetBinding(Colour,
					bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
				if (Stencil)
				{
					Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(Stencil,
						ERenderTargetLoadAction::ENoAction, ERenderTargetLoadAction::EClear,
						FExclusiveDepthStencil::DepthNop_StencilWrite);
				}

				// Never culled: with every glyph run held back for the screen there is nothing to draw, and
				// the pass is then here only to clear the target - which is exactly when it must still run.
				GraphBuilder.AddPass(RDG_EVENT_NAME("FolioLayer"), Parameters,
					ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
					[Parameters, Size, Clip, Draws = MoveTemp(Draws)](FRHICommandList& RHICmdList)
					{
						const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
						TShaderMapRef<FFolioUIVS> VertexShader(ShaderMap);
						TShaderMapRef<FFolioUIPS> PixelShader(ShaderMap);
						TShaderMapRef<FFolioUIMsdfPS> FieldShader(ShaderMap);
						TShaderMapRef<FFolioUIMaskPS> MaskShader(ShaderMap);

						RHICmdList.SetViewport(0, 0, 0.0f, Size.X, Size.Y, 1.0f);
						RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

						// One buffer pair for the replay rather than one per draw.
						//
						// A layer's geometry is rebuilt whenever it is drawn, so nothing is worth keeping
						// between frames - but creating, locking and unlocking a volatile buffer is a fixed
						// cost per call, and a shadow layer is dozens of draws. Two 236x50dp drop shadows
						// measured 74 buffer pairs and 0.83ms of render thread a frame that way, which was
						// most of what a layer cost. Concatenated, it is two.
						FBufferRHIRef VertexBuffer;
						FBufferRHIRef IndexBuffer;
						TArray<uint32, TInlineAllocator<64>> BaseVertex;
						TArray<uint32, TInlineAllocator<64>> FirstIndex;
						{
							SCOPE_CYCLE_COUNTER(STAT_FolioLayerBuffers);

							uint32 TotalVertices = 0;
							uint32 TotalIndices = 0;
							BaseVertex.Reserve(Draws.Num());
							FirstIndex.Reserve(Draws.Num());

							for (const FFolioDraw& Draw : Draws)
							{
								BaseVertex.Add(TotalVertices);
								FirstIndex.Add(TotalIndices);
								TotalVertices += Draw.Vertices.Num();
								TotalIndices += Draw.Indices.Num();
							}

							if (TotalVertices > 0 && TotalIndices > 0)
							{
								const uint32 VertexBytes = TotalVertices * sizeof(FFolioUIVertex);
								VertexBuffer = RHICmdList.CreateBuffer(
									FRHIBufferCreateDesc::CreateVertex(TEXT("FolioLayerVertices"), VertexBytes)
										.AddUsage(EBufferUsageFlags::Volatile)
										.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
								uint8* VertexData = (uint8*)RHICmdList.LockBuffer(VertexBuffer, 0, VertexBytes,
									RLM_WriteOnly);

								const uint32 IndexBytes = TotalIndices * sizeof(uint32);
								IndexBuffer = RHICmdList.CreateBuffer(
									FRHIBufferCreateDesc::CreateIndex(TEXT("FolioLayerIndices"), IndexBytes,
										sizeof(uint32))
										.AddUsage(EBufferUsageFlags::Volatile)
										.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
								uint8* IndexData = (uint8*)RHICmdList.LockBuffer(IndexBuffer, 0, IndexBytes,
									RLM_WriteOnly);

								for (const FFolioDraw& Draw : Draws)
								{
									const uint32 Vertices = Draw.Vertices.Num() * sizeof(FFolioUIVertex);
									FMemory::Memcpy(VertexData, Draw.Vertices.GetData(), Vertices);
									VertexData += Vertices;

									// Indices stay draw-local; BaseVertexIndex on the draw call is what
									// offsets them into the shared buffer.
									const uint32 Indices = Draw.Indices.Num() * sizeof(uint32);
									FMemory::Memcpy(IndexData, Draw.Indices.GetData(), Indices);
									IndexData += Indices;
								}

								RHICmdList.UnlockBuffer(VertexBuffer);
								RHICmdList.UnlockBuffer(IndexBuffer);
							}
						}

						if (!VertexBuffer || !IndexBuffer)
						{
							return;
						}

						const FIntRect Whole(0, 0, Size.X, Size.Y);
						const FVector2f Ratio(Clip.X > 0 ? float(Size.X) / float(Clip.X) : 1.0f,
							Clip.Y > 0 ? float(Size.Y) / float(Clip.Y) : 1.0f);
						bool bScissored = false;

						for (int32 DrawIndex = 0; DrawIndex < Draws.Num(); ++DrawIndex)
						{
							const FFolioDraw& Draw = Draws[DrawIndex];
							const bool bField = Draw.Kind == FFolioDraw::EKind::Field;
							const bool bMaskPass = Draw.Kind != FFolioDraw::EKind::Geometry && !bField;

							FGraphicsPipelineStateInitializer State;
							RHICmdList.ApplyCachedRenderTargets(State);
							State.PrimitiveType = PT_TriangleList;
							State.BoundShaderState.VertexDeclarationRHI = GFolioUIVertexDeclaration.VertexDeclarationRHI;
							State.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
							State.BoundShaderState.PixelShaderRHI = bMaskPass
								? static_cast<FRHIPixelShader*>(MaskShader.GetPixelShader())
								: bField
									? static_cast<FRHIPixelShader*>(FieldShader.GetPixelShader())
									: static_cast<FRHIPixelShader*>(PixelShader.GetPixelShader());
							State.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

							if (bMaskPass)
							{
								// Colour writes off: a mask contributes stencil and nothing else.
								State.BlendState = TStaticBlendState<CW_NONE>::GetRHI();
								State.DepthStencilState = TStaticDepthStencilState<false, CF_Always,
									true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI();
							}
							else
							{
								// Premultiplied source over destination.
								State.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One,
									BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

								if (Draw.bMasked)
								{
									// Equal keeps the shape; NotEqual keeps its complement, which
									// is what an outer box-shadow needs.
									State.DepthStencilState = Draw.bMaskInverted
										? TStaticDepthStencilState<false, CF_Always, true, CF_NotEqual,
											SO_Keep, SO_Keep, SO_Keep>::GetRHI()
										: TStaticDepthStencilState<false, CF_Always, true, CF_Equal,
											SO_Keep, SO_Keep, SO_Keep>::GetRHI();
								}
								else
								{
									State.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
								}
							}

							{
								SCOPE_CYCLE_COUNTER(STAT_FolioLayerState);
								SetGraphicsPipelineState(RHICmdList, State, MaskStencilRef);
								RHICmdList.SetStencilRef(MaskStencilRef);

								// After the pipeline state, every time. Binding a stream before one is
								// bound is dropped, and the draw reads nothing - which showed as a
								// surface that rendered completely empty.
								RHICmdList.SetStreamSource(0, VertexBuffer, 0);
							}

							FFolioUIParameters PassParameters = *Parameters;
							PassParameters.MsdfField = Draw.Field;
							PassParameters.MsdfEffect = Draw.Effect;
							PassParameters.UIOptions =
								FVector4f(Draw.bStraightAlpha ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
							if (Draw.bTiles)
							{
								PassParameters.UISampler =
									TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							}
							if (Draw.Texture.IsValid() && Draw.Texture->GetResource())
							{
								if (FRHITexture* Bound = Draw.Texture->GetResource()->GetTexture2DRHI())
								{
									PassParameters.UITexture = Bound;
								}
							}

							SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters);
							if (bField)
							{
								SetShaderParameters(RHICmdList, FieldShader, FieldShader.GetPixelShader(), PassParameters);
							}
							else if (!bMaskPass)
							{
								SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters);
							}

							// `overflow: hidden`, which the screen path gives to Slate as a clip zone and a
							// recording has to carry itself. Clamped to the layer, because RmlUi's scissor is
							// against the whole context and a layer is only as big as the element that wanted
							// it - and the RHI rejects a rectangle reaching past its viewport.
							// Into the target's pixels. The scissor arrives in the space the geometry was
							// recorded in, and the vertex shader scales that space onto the target, so a
							// target that is not the same size scales the rectangle by the same ratio.
							FIntRect Scissor(
								FMath::FloorToInt(Draw.Scissor.Min.X * Ratio.X),
								FMath::FloorToInt(Draw.Scissor.Min.Y * Ratio.Y),
								FMath::CeilToInt(Draw.Scissor.Max.X * Ratio.X),
								FMath::CeilToInt(Draw.Scissor.Max.Y * Ratio.Y));

							Scissor.Clip(Whole);
							Scissor.Max.X = FMath::Max(Scissor.Min.X, Scissor.Max.X);
							Scissor.Max.Y = FMath::Max(Scissor.Min.Y, Scissor.Max.Y);

							// A rectangle covering the whole layer is left to the viewport. It is the common
							// case, since RmlUi sets the scissor to the context whenever nothing is clipping,
							// and an empty intersection still has to be set: that draw is wholly outside its
							// clip and turning the scissor off would draw the one thing meant to be hidden.
							const bool bWanted = Draw.Scissor.Area() > 0 && Scissor != Whole;

							if (bWanted || bScissored)
							{
								RHICmdList.SetScissorRect(bWanted, Scissor.Min.X, Scissor.Min.Y,
									Scissor.Max.X, Scissor.Max.Y);
								bScissored = bWanted;
							}

							RHICmdList.DrawIndexedPrimitive(IndexBuffer, BaseVertex[DrawIndex], 0,
								Draw.Vertices.Num(), FirstIndex[DrawIndex], Draw.Indices.Num() / 3, 1);
						}
					});
			}

			GraphBuilder.Execute();
		});
}

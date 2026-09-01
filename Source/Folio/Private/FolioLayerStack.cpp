#include "FolioLayerStack.h"

#include "FolioColourShader.h"
#include "FolioMaskShader.h"
#include "FolioResolveShader.h"

#include "FolioInsights.h"

#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget2D.h"
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
	Scale = 1;
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

UTextureRenderTarget2D* FFolioLayerStack::Acquire(const FIntPoint Size)
{
	UTextureRenderTarget2D* Target = Obtain(Size);
	if (Target)
	{
		Stack.Add({ TStrongObjectPtr<UTextureRenderTarget2D>(Target), Size });
	}

	return Target;
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
	if (Target)
	{
		Pool.Add(FIntPoint(Target->SizeX, Target->SizeY), TStrongObjectPtr<UTextureRenderTarget2D>(Target));
	}
}

bool FFolioLayerStack::Push(const FIntPoint Size, const FIntPoint Origin)
{
	if (Size.X <= 0 || Size.Y <= 0 || !Acquire(FIntPoint(Size.X * Scale, Size.Y * Scale)))
	{
		return false;
	}

	Stack.Last().Origin = Origin;
	Stack.Last().ClipSize = Size;
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

	if (Size.X <= 0 || Size.Y <= 0 || !Acquire(Size))
	{
		return false;
	}

	// Carried too, or a layer over a supersampled surface would replay the same geometry at the
	// target's scale instead of the document's.
	Stack.Last().ClipSize = Clip;
	Stack.Last().Origin = Origin;
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
	const TArrayView<const Folio::FColourFilter> Colour, UTextureRenderTarget2D* Mask)
{
	if (Stack.IsEmpty())
	{
		return nullptr;
	}

	FLayer& Layer = Stack.Last();
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
	Folio::ApplyMask(Layer.Target.Get(), Mask, {});

	UTextureRenderTarget2D* Detached = Layer.Target.Get();

	// The caller owns the detached target now, so the layer needs a fresh one to keep drawing
	// into. Deliberately not from the pool: RmlUi keeps the saved one indefinitely.
	UTextureRenderTarget2D* Replacement = NewObject<UTextureRenderTarget2D>();
	Replacement->RenderTargetFormat = RTF_RGBA8;
	Replacement->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	Replacement->bAutoGenerateMips = false;
	Replacement->bForceLinearGamma = false;
	Replacement->InitAutoFormat(Layer.Size.X, Layer.Size.Y);
	Replacement->UpdateResourceImmediate(true);

	Layer.Target = TStrongObjectPtr<UTextureRenderTarget2D>(Replacement);
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

	Folio::ApplyMask(Composited, Mask, {});

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

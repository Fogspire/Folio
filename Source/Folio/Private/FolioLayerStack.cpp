#include "FolioLayerStack.h"

#include "FolioColourShader.h"
#include "FolioMaskShader.h"

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
	Stack.Empty();
	Pool.Empty();
}

UTextureRenderTarget2D* FFolioLayerStack::Acquire(const FIntPoint Size)
{
	TArray<TStrongObjectPtr<UTextureRenderTarget2D>> Available;
	Pool.MultiFind(Size, Available);

	if (Available.Num() > 0 && Available[0].IsValid())
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Found = Available[0];
		Pool.RemoveSingle(Size, Found);
		Found->UpdateResourceImmediate(true);

		Stack.Add({ Found, Size });
		return Found.Get();
	}

	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
	Target->RenderTargetFormat = RTF_RGBA8;

	// Premultiplied transparent, not opaque black: a layer that starts black fringes every
	// edge it later composites over.
	Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	Target->bAutoGenerateMips = false;
	Target->bForceLinearGamma = false;
	Target->InitAutoFormat(Size.X, Size.Y);
	Target->UpdateResourceImmediate(true);

	Stack.Add({ TStrongObjectPtr<UTextureRenderTarget2D>(Target), Size });
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
	if (Size.X <= 0 || Size.Y <= 0 || !Acquire(Size))
	{
		return false;
	}

	Stack.Last().Origin = Origin;
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
	UTexture* Texture, const FVector2f Offset)
{
	if (Stack.IsEmpty() || Indices.Num() < 3 || !Texture)
	{
		return;
	}

	FLayer& Layer = Stack.Last();
	Layer.Draws.AddTriangles(Vertices, Indices, Texture, Offset,
		Layer.bClipMaskEnabled, Layer.bClipMaskInverted);
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

	if (BlurSigma > 0.0f)
	{
		Folio::BlurRenderTarget(Layer.Target.Get(), BlurSigma);
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

	if (BlurSigma > 0.0f)
	{
		Folio::BlurRenderTarget(Source.Target.Get(), BlurSigma);
	}

	Folio::ApplyMask(Source.Target.Get(), Mask, {});

	// Compositing is a full-target quad drawn into the layer below, recorded like any other
	// draw so it stays ordered with the geometry around it.
	FLayer& Destination = Stack[Stack.Num() - 2];

	FFolioDraw Draw;
	Draw.Kind = FFolioDraw::EKind::Geometry;
	Draw.Texture.Reset(Source.Target.Get());
	Draw.bMasked = false;

	const FVector2f Size(Source.Size.X, Source.Size.Y);
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
	Destination.Sampled.Add(Source.Target);
	Source.bHandedOff = true;
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

	if (Layer.Draws.IsEmpty() || !Layer.Target.IsValid())
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

	ENQUEUE_RENDER_COMMAND(FolioResolveLayer)(
		[Resource, Size, Clip, bClear, Draws = MoveTemp(Draws)](FRHICommandListImmediate& RHICmdList) mutable
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef Colour = RegisterExternalTexture(GraphBuilder,
				Resource->GetRenderTargetTexture(), TEXT("FolioLayerColour"));

			// The stencil only has to live as long as this layer's replay, which is exactly
			// why the draws are recorded and resolved together rather than one at a time.
			const FRDGTextureDesc StencilDesc = FRDGTextureDesc::Create2D(Size, PF_DepthStencil,
				FClearValueBinding::DepthZero, TexCreate_DepthStencilTargetable);
			FRDGTextureRef Stencil = GraphBuilder.CreateTexture(StencilDesc, TEXT("FolioLayerStencil"));

			FFolioUIParameters* Parameters = GraphBuilder.AllocParameters<FFolioUIParameters>();
			Parameters->UISampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Parameters->UITexture = GWhiteTexture->TextureRHI;

			// Document pixels to clip space. Y is flipped: RmlUi's origin is top left. Against the recorded
			// space rather than the target, so a larger target scales the same geometry up instead of
			// leaving it in a corner.
			Parameters->ClipTransform = FVector4f(2.0f / Clip.X, -2.0f / Clip.Y, -1.0f, 1.0f);
			Parameters->RenderTargets[0] = FRenderTargetBinding(Colour,
				bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
			Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(Stencil,
				ERenderTargetLoadAction::ENoAction, ERenderTargetLoadAction::EClear,
				FExclusiveDepthStencil::DepthNop_StencilWrite);

			GraphBuilder.AddPass(RDG_EVENT_NAME("FolioLayer"), Parameters, ERDGPassFlags::Raster,
				[Parameters, Size, Clip, Draws = MoveTemp(Draws)](FRHICommandList& RHICmdList)
				{
					const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FFolioUIVS> VertexShader(ShaderMap);
					TShaderMapRef<FFolioUIPS> PixelShader(ShaderMap);
					TShaderMapRef<FFolioUIMsdfPS> FieldShader(ShaderMap);
					TShaderMapRef<FFolioUIMaskPS> MaskShader(ShaderMap);

					RHICmdList.SetViewport(0, 0, 0.0f, Size.X, Size.Y, 1.0f);
					RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

					const FIntRect Whole(0, 0, Size.X, Size.Y);
					const FVector2f Ratio(Clip.X > 0 ? float(Size.X) / float(Clip.X) : 1.0f,
						Clip.Y > 0 ? float(Size.Y) / float(Clip.Y) : 1.0f);
					bool bScissored = false;

					for (const FFolioDraw& Draw : Draws)
					{
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

						SetGraphicsPipelineState(RHICmdList, State, MaskStencilRef);
						RHICmdList.SetStencilRef(MaskStencilRef);

						FFolioUIParameters PassParameters = *Parameters;
						PassParameters.MsdfField = Draw.Field;
						PassParameters.MsdfEffect = Draw.Effect;
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

						// Volatile buffers: a layer's geometry is rebuilt whenever it is drawn,
						// so there is nothing worth keeping between frames.
						const uint32 VertexBytes = Draw.Vertices.Num() * sizeof(FFolioUIVertex);
						FBufferRHIRef VertexBuffer = RHICmdList.CreateBuffer(
							FRHIBufferCreateDesc::CreateVertex(TEXT("FolioLayerVertices"), VertexBytes)
								.AddUsage(EBufferUsageFlags::Volatile)
								.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
						void* VertexData = RHICmdList.LockBuffer(VertexBuffer, 0, VertexBytes, RLM_WriteOnly);
						FMemory::Memcpy(VertexData, Draw.Vertices.GetData(), VertexBytes);
						RHICmdList.UnlockBuffer(VertexBuffer);

						const uint32 IndexBytes = Draw.Indices.Num() * sizeof(uint32);
						FBufferRHIRef IndexBuffer = RHICmdList.CreateBuffer(
							FRHIBufferCreateDesc::CreateIndex(TEXT("FolioLayerIndices"), IndexBytes, sizeof(uint32))
								.AddUsage(EBufferUsageFlags::Volatile)
								.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
						void* IndexData = RHICmdList.LockBuffer(IndexBuffer, 0, IndexBytes, RLM_WriteOnly);
						FMemory::Memcpy(IndexData, Draw.Indices.GetData(), IndexBytes);
						RHICmdList.UnlockBuffer(IndexBuffer);

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

						RHICmdList.SetStreamSource(0, VertexBuffer, 0);
						RHICmdList.DrawIndexedPrimitive(IndexBuffer, 0, 0, Draw.Vertices.Num(), 0,
							Draw.Indices.Num() / 3, 1);
					}
				});

			GraphBuilder.Execute();
		});
}

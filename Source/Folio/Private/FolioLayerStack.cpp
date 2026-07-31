#include "FolioLayerStack.h"

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

bool FFolioLayerStack::Push(const FIntPoint Size)
{
	return Size.X > 0 && Size.Y > 0 && Acquire(Size) != nullptr;
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
	Resolve(Layer);

	if (!Layer.bCallerOwned)
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

UTextureRenderTarget2D* FFolioLayerStack::DetachCurrent(const float BlurSigma)
{
	if (Stack.IsEmpty())
	{
		return nullptr;
	}

	FLayer& Layer = Stack.Last();
	Resolve(Layer);

	if (BlurSigma > 0.0f)
	{
		Folio::BlurRenderTarget(Layer.Target.Get(), BlurSigma);
	}

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

void FFolioLayerStack::CompositeDown(const bool bBlend, const float BlurSigma)
{
	if (Stack.Num() < 2)
	{
		return;
	}

	FLayer& Source = Stack.Last();
	Resolve(Source);

	if (BlurSigma > 0.0f)
	{
		Folio::BlurRenderTarget(Source.Target.Get(), BlurSigma);
	}

	// Compositing is a full-target quad drawn into the layer below, recorded like any other
	// draw so it stays ordered with the geometry around it.
	FLayer& Destination = Stack[Stack.Num() - 2];

	FFolioDraw Draw;
	Draw.Kind = FFolioDraw::EKind::Geometry;
	Draw.Texture.Reset(Source.Target.Get());
	Draw.bMasked = false;

	const FVector2f Size(Source.Size.X, Source.Size.Y);
	const FLinearColor White(1.0f, 1.0f, 1.0f, 1.0f);

	Draw.Vertices.Add({ FVector2f(0.0f, 0.0f),     FVector2f(0.0f, 0.0f), White });
	Draw.Vertices.Add({ FVector2f(Size.X, 0.0f),   FVector2f(1.0f, 0.0f), White });
	Draw.Vertices.Add({ FVector2f(Size.X, Size.Y), FVector2f(1.0f, 1.0f), White });
	Draw.Vertices.Add({ FVector2f(0.0f, Size.Y),   FVector2f(0.0f, 1.0f), White });
	Draw.Indices = { 0, 1, 2, 0, 2, 3 };

	Destination.Draws.GetDraws().Add(MoveTemp(Draw));
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

	ENQUEUE_RENDER_COMMAND(FolioResolveLayer)(
		[Resource, Size, Clip, Draws = MoveTemp(Draws)](FRHICommandListImmediate& RHICmdList) mutable
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
			Parameters->RenderTargets[0] = FRenderTargetBinding(Colour, ERenderTargetLoadAction::ELoad);
			Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(Stencil,
				ERenderTargetLoadAction::ENoAction, ERenderTargetLoadAction::EClear,
				FExclusiveDepthStencil::DepthNop_StencilWrite);

			GraphBuilder.AddPass(RDG_EVENT_NAME("FolioLayer"), Parameters, ERDGPassFlags::Raster,
				[Parameters, Size, Draws = MoveTemp(Draws)](FRHICommandList& RHICmdList)
				{
					const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FFolioUIVS> VertexShader(ShaderMap);
					TShaderMapRef<FFolioUIPS> PixelShader(ShaderMap);
					TShaderMapRef<FFolioUIMsdfPS> FieldShader(ShaderMap);
					TShaderMapRef<FFolioUIMaskPS> MaskShader(ShaderMap);

					RHICmdList.SetViewport(0, 0, 0.0f, Size.X, Size.Y, 1.0f);

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

						RHICmdList.SetStreamSource(0, VertexBuffer, 0);
						RHICmdList.DrawIndexedPrimitive(IndexBuffer, 0, 0, Draw.Vertices.Num(), 0,
							Draw.Indices.Num() / 3, 1);
					}
				});

			GraphBuilder.Execute();
		});
}

#include "FolioWorldPass.h"

#include "FolioDrawList.h"
#include "FolioModule.h"
#include "FolioWorldDocument.h"
#include "FolioWorldShader.h"

#include "Engine/World.h"
#include "PostProcess/PostProcessInputs.h"
// FMobilePostProcessingInputs is declared nowhere else. Renderer/Internal carries the deferred
// twin; the mobile one stayed private.
#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "SceneInterface.h"
#include "ScreenPass.h"
#include "SystemTextures.h"

namespace
{
	TAutoConsoleVariable<bool> CVarWorldDocuments(
		TEXT("folio.World"),
		true,
		TEXT("Draw world-space documents. Off draws nothing by either route, which is how to tell a Folio "
			"problem from a scene problem."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarWorldOcclusion(
		TEXT("folio.World.Occlusion"),
		1,
		TEXT("Occlusion for documents drawn after the tonemapper, where there is no depth buffer to test "
			"against. 0 draws every document through the world, which separates an occlusion bug from a "
			"transform bug. 1 rejects fragments behind scene geometry. 2 replaces the document with what the "
			"test reads: red is scene depth, green is the document's own depth amplified 20x, blue means "
			"occluded. Red anywhere near white means the depth lookup is wrong rather than the comparison. "
			"In-scene documents ignore this: they test the real depth buffer."),
		ECVF_RenderThreadSafe);
}

FFolioWorldPass::FFolioWorldPass(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FFolioWorldPass::IsActiveThisFrame_Internal(const FSceneViewExtensionContext&) const
{
	return CVarWorldDocuments.GetValueOnAnyThread() && !UFolioWorldDocument::GetLive().IsEmpty();
}

void FFolioWorldPass::ForgetDrawLists(TArray<const FFolioDrawList*> Lists)
{
	ENQUEUE_RENDER_COMMAND(FolioForgetSurfaces)(
		[this, Lists = MoveTemp(Lists)](FRHICommandListImmediate&)
		{
			if (!RenderSurfaces.IsValid())
			{
				return;
			}

			TSharedRef<TArray<FSurface>> Kept = MakeShared<TArray<FSurface>>();
			for (const FSurface& Surface : *RenderSurfaces)
			{
				if (!Lists.Contains(Surface.Draws.Get()))
				{
					Kept->Add(Surface);
				}
			}
			RenderSurfaces = Kept;
		});
}

bool FFolioWorldPass::HasSurfaces(const bool bInScene) const
{
	return RenderSurfaces.IsValid() && RenderSurfaces->ContainsByPredicate(
		[bInScene](const FSurface& Surface) { return Surface.bInScene == bInScene; });
}

void FFolioWorldPass::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Collected here because this is the last point on the game thread where a component can safely be
	// touched. Everything the pass needs is taken now, and nothing reads a UObject afterwards.
	TSharedRef<TArray<FSurface>> Collected = MakeShared<TArray<FSurface>>();

	// The live list is process-wide, and so is a view extension. Without this a document placed in the
	// level draws into every other scene the editor renders: blueprint and material preview viewports,
	// asset thumbnails, anything with its own world.
	const UWorld* FamilyWorld = InViewFamily.Scene ? InViewFamily.Scene->GetWorld() : nullptr;

	if (FamilyWorld)
	{
		for (UFolioWorldDocument* Document : UFolioWorldDocument::GetLive())
		{
			if (!Document || Document->GetWorld() != FamilyWorld)
			{
				continue;
			}

			// Hidden the way any component is: a game shows and hides a surface without tearing its document down.
			if (!Document->IsVisible())
			{
				continue;
			}

			// A surface renders itself into a target for a material to sample, so neither route here draws it.
			if (Document->DrawMode == EFolioWorldDrawMode::Surface)
			{
				continue;
			}

			TSharedPtr<const FFolioDrawList> Draws = Document->GetDrawList();
			if (!Draws.IsValid() || Draws->IsEmpty())
			{
				continue;
			}

			FSurface& Surface = Collected->AddDefaulted_GetRef();
			Surface.Draws = MoveTemp(Draws);
			Surface.DocumentSize = Document->GetDocumentSize();
			Surface.bInScene = Document->DrawMode == EFolioWorldDrawMode::InScene;
			Surface.bAlwaysOnTop = Document->bAlwaysOnTop;
			Document->GetSurface(Surface.Centre, Surface.Right, Surface.Up);
		}
	}

	// Handed over rather than assigned: render commands run in order, so this lands before the passes
	// for this frame and after the ones already issued for the last. An empty snapshot is enqueued too,
	// or a scene with nothing in it keeps drawing whatever the previous family collected.
	ENQUEUE_RENDER_COMMAND(FolioWorldSurfaces)(
		[this, Collected](FRHICommandListImmediate&)
		{
			RenderSurfaces = Collected;
		});
}

void FFolioWorldPass::SubscribeToPostProcessingPass(const EPostProcessingPass Pass, const FSceneView&,
	FPostProcessingPassDelegateArray& InOutPassCallbacks, const bool)
{
	// Tonemap is BL_SceneColorAfterTonemapping, which is after the temporal resolve. That is the whole
	// point: subscribing anywhere earlier would put the document back inside TAA.
	if (Pass != EPostProcessingPass::Tonemap || !HasSurfaces(/*bInScene*/ false))
	{
		return;
	}

	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FFolioWorldPass::Draw));
}

void FFolioWorldPass::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
	const FPostProcessingInputs& Inputs)
{
	// Before post-processing rather than after the base pass, which is the other hook that looks right and
	// is not: in a deferred renderer scene colour is still being accumulated then, so lighting would be
	// added on top of the document. Here the scene is lit and translucency is done, and the temporal
	// resolve has not run - which is what makes this the in-scene route rather than a second copy of the
	// after-tonemap one.
	if (!HasSurfaces(/*bInScene*/ true) || !Inputs.SceneTextures)
	{
		return;
	}

	if (const FSceneTextureUniformParameters* SceneTextures = Inputs.SceneTextures->GetContents())
	{
		DrawInScene(GraphBuilder, View, SceneTextures->SceneColorTexture, SceneTextures->SceneDepthTexture);
	}
}

void FFolioWorldPass::PrePostProcessPassMobile_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
	const FMobilePostProcessingInputs& Inputs)
{
	if (!HasSurfaces(/*bInScene*/ true) || !Inputs.SceneTextures)
	{
		return;
	}

	if (const FMobileSceneTextureUniformParameters* SceneTextures = Inputs.SceneTextures->GetContents())
	{
		DrawInScene(GraphBuilder, View, SceneTextures->SceneColorTexture, SceneTextures->SceneDepthTexture);
	}
}

void FFolioWorldPass::DrawInScene(FRDGBuilder& GraphBuilder, const FSceneView& View,
	FRDGTextureRef SceneColour, FRDGTextureRef SceneDepth)
{
	if (!SceneColour || !SceneDepth)
	{
		return;
	}

	const FIntPoint Extent = SceneColour->Desc.Extent;
	if (Extent.X <= 0 || Extent.Y <= 0)
	{
		return;
	}

	FTarget Target;
	Target.Colour = FRenderTargetBinding(SceneColour, ERenderTargetLoadAction::ELoad);

	// Read only: a document is translucent, so it tests against the scene without becoming part of it.
	Target.Depth = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad,
		ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);

	// The whole texture, not the view's rectangle, because the render-resolution rectangle is not reachable
	// from a scene view. The vertex shader corrects for that from the view uniform buffer instead.
	Target.Viewport = FIntRect(FIntPoint::ZeroValue, Extent);
	Target.bDepthTest = true;
	Target.SceneTarget = FVector4f(1.0f / float(Extent.X), 1.0f / float(Extent.Y), 1.0f, 1.0f);

	const FMatrix ViewProjection =
		View.ViewMatrices.GetWorldToView() * View.ViewMatrices.GetViewToClipNoAA();

	for (const FSurface& Surface : *RenderSurfaces)
	{
		if (Surface.bInScene)
		{
			AddSurface(GraphBuilder, View, Surface, ViewProjection, Target);
		}
	}
}

FScreenPassTexture FFolioWorldPass::Draw(FRDGBuilder& GraphBuilder, const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	// The scene colour texture itself, not a copy of it. CopyFromSlice allocates a new texture, and
	// returning that from this pass replaced scene colour in the post chain with something the scene had
	// never been drawn into - a black screen with a correctly lit depth buffer behind it.
	const FScreenPassTextureSlice& Slice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	const FScreenPassTexture SceneColour(
		Slice.TextureSRV ? Slice.TextureSRV->Desc.Texture : nullptr, Slice.ViewRect);

	if (!SceneColour.IsValid())
	{
		return SceneColour;
	}

	// The pass writes over the scene rather than producing a new image, so it needs somewhere it may
	// write. OverrideOutput is supplied when this happens to be the last pass; otherwise the scene colour
	// is written in place, which is what a post-process material at this point does.
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (Output.IsValid())
	{
		AddDrawTexturePass(GraphBuilder, View, SceneColour, Output);

		// The override arrives with a load action that discards, because whoever handed it over expected a
		// single pass to fill it. The copy above has already filled it, so the document's own pass has to
		// load: leaving the discard in place clears the scene back to black and draws the document onto
		// nothing, which is a black screen with a correctly lit depth buffer behind it.
		Output.LoadAction = ERenderTargetLoadAction::ELoad;
	}
	else
	{
		Output = FScreenPassRenderTarget(SceneColour, ERenderTargetLoadAction::ELoad);
	}

	// Either uniform buffer, because the mobile renderer fills its own and leaves the deferred one
	// null. Read only the first and occlusion silently turns itself off under mobile forward, which
	// looks like the depth test failing rather than never running.
	FRDGTextureRef SceneDepth = nullptr;
	if (Inputs.SceneTextures.SceneTextures)
	{
		SceneDepth = Inputs.SceneTextures.SceneTextures->GetContents()->SceneDepthTexture;
	}
	else if (Inputs.SceneTextures.MobileSceneTextures)
	{
		SceneDepth = Inputs.SceneTextures.MobileSceneTextures->GetContents()->SceneDepthTexture;
	}

	FTarget Target;
	Target.Colour = Output.GetRenderTargetBinding();
	Target.Viewport = Output.ViewRect;
	Target.OcclusionMode = SceneDepth != nullptr
		? FMath::Clamp(CVarWorldOcclusion.GetValueOnRenderThread(), 0, 2)
		: 0;
	Target.DepthTexture = Target.OcclusionMode > 0
		? SceneDepth
		: GSystemTextures.GetBlackDummy(GraphBuilder);

	// The rect rather than the extent: a fragment reaches its depth pixel through a screen position, and
	// SV_Position is absolute in the render target while a screen position is relative to the view. Doing it
	// that way is also what keeps this correct under an upscaler, where the two targets differ in resolution
	// and in padding, and where getting it wrong looks right at 100% screen percentage.
	Target.OutputRect = FVector4f(
		float(Target.Viewport.Min.X), float(Target.Viewport.Min.Y),
		1.0f / float(FMath::Max(1, Target.Viewport.Width())),
		1.0f / float(FMath::Max(1, Target.Viewport.Height())));

	if (Target.OcclusionMode == 2)
	{
		UE_LOG(LogFolio, Display,
			TEXT("folio.World: depth %dx%d, output %dx%d, output rect (%d,%d)-(%d,%d), unscaled view rect "
				"(%d,%d)-(%d,%d)"),
			SceneDepth->Desc.Extent.X, SceneDepth->Desc.Extent.Y,
			Output.Texture->Desc.Extent.X, Output.Texture->Desc.Extent.Y,
			Target.Viewport.Min.X, Target.Viewport.Min.Y, Target.Viewport.Max.X, Target.Viewport.Max.Y,
			View.UnscaledViewRect.Min.X, View.UnscaledViewRect.Min.Y,
			View.UnscaledViewRect.Max.X, View.UnscaledViewRect.Max.Y);
	}

	// The unjittered projection. Every temporal method offsets the projection by a subpixel amount that
	// differs each frame, and the resolve undoes it - but this pass runs after the resolve, so a document
	// drawn with the jittered matrix inherits the offset without the correction and shimmers in place.
	const FMatrix ViewProjection =
		View.ViewMatrices.GetWorldToView() * View.ViewMatrices.GetViewToClipNoAA();

	// On-top surfaces last, so an ordinary world screen overlapping one on screen never paints over it.
	for (const bool bOnTop : { false, true })
	{
		for (const FSurface& Surface : *RenderSurfaces)
		{
			if (!Surface.bInScene && Surface.bAlwaysOnTop == bOnTop)
			{
				AddSurface(GraphBuilder, View, Surface, ViewProjection, Target);
			}
		}
	}

	return MoveTemp(Output);
}

void FFolioWorldPass::AddSurface(FRDGBuilder& GraphBuilder, const FSceneView& View, const FSurface& Surface,
	const FMatrix& ViewProjection, const FTarget& Target) const
{
	if (!Surface.Draws.IsValid() || Surface.Draws->IsEmpty())
	{
		return;
	}

	// Document pixels to world, then world to clip, as one matrix the vertex shader applies.
	//
	// The document's origin is its top left with Y running down, while the surface is described as a
	// centre and two half extents, so the basis is built to put pixel (0,0) at the top left corner.
	const FVector Origin = Surface.Centre - Surface.Right + Surface.Up;
	const FVector PerPixelX = (Surface.Right * 2.0) / double(FMath::Max(1, Surface.DocumentSize.X));
	const FVector PerPixelY = (-Surface.Up * 2.0) / double(FMath::Max(1, Surface.DocumentSize.Y));
	const FVector Normal = FVector::CrossProduct(PerPixelX, PerPixelY).GetSafeNormal();

	FMatrix DocumentToWorld = FMatrix::Identity;
	DocumentToWorld.SetAxes(&PerPixelX, &PerPixelY, &Normal, &Origin);

	// Held by value, so the list cannot be replaced from under the pass while the frame is in flight.
	const TSharedPtr<const FFolioDrawList> Draws = Surface.Draws;

	FFolioWorldParameters* Parameters = GraphBuilder.AllocParameters<FFolioWorldParameters>();
	Parameters->UISampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->UITexture = GWhiteTexture->TextureRHI;
	Parameters->DocumentToClip = FMatrix44f(DocumentToWorld * ViewProjection);
	Parameters->MsdfField = FVector4f::Zero();
	Parameters->MsdfEffect = FVector4f::Zero();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->FolioSceneDepth = Target.DepthTexture
		? Target.DepthTexture
		: GSystemTextures.GetBlackDummy(GraphBuilder);
	Parameters->FolioOutputRect = Target.OutputRect;
	// Mode 0 draws through geometry. The debug view (2) is kept so it still reads for every surface.
	Parameters->FolioOcclusionMode = (Surface.bAlwaysOnTop && Target.OcclusionMode != 2)
		? 0.0f
		: float(Target.OcclusionMode);
	Parameters->FolioSceneTarget = Target.SceneTarget;
	Parameters->RenderTargets[0] = Target.Colour;
	Parameters->RenderTargets.DepthStencil = Target.Depth;

	const FIntRect Viewport = Target.Viewport;
	const bool bDepthTest = Target.bDepthTest;

	GraphBuilder.AddPass(RDG_EVENT_NAME("FolioWorldDocument"), Parameters, ERDGPassFlags::Raster,
		[Parameters, Viewport, bDepthTest, Draws](FRHICommandList& RHICmdList)
		{
			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FFolioWorldVS> VertexShader(ShaderMap);
			TShaderMapRef<FFolioWorldPS> PixelShader(ShaderMap);
			TShaderMapRef<FFolioWorldMsdfPS> FieldShader(ShaderMap);

			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f,
				Viewport.Max.X, Viewport.Max.Y, 1.0f);

			// One buffer pair for the whole document rather than one per draw. Creating, locking and
			// unlocking a volatile buffer is a fixed cost per call, and a document is dozens of draws.
			const TArray<FFolioDraw>& All = Draws->GetDraws();

			FBufferRHIRef VertexBuffer;
			FBufferRHIRef IndexBuffer;
			TArray<uint32, TInlineAllocator<64>> BaseVertex;
			TArray<uint32, TInlineAllocator<64>> FirstIndex;
			{
				uint32 TotalVertices = 0;
				uint32 TotalIndices = 0;
				BaseVertex.Reserve(All.Num());
				FirstIndex.Reserve(All.Num());

				for (const FFolioDraw& Draw : All)
				{
					BaseVertex.Add(TotalVertices);
					FirstIndex.Add(TotalIndices);
					TotalVertices += Draw.Vertices.Num();
					TotalIndices += Draw.Indices.Num();
				}

				if (TotalVertices == 0 || TotalIndices == 0)
				{
					return;
				}

				const uint32 VertexBytes = TotalVertices * sizeof(FFolioUIVertex);
				VertexBuffer = RHICmdList.CreateBuffer(
					FRHIBufferCreateDesc::CreateVertex(TEXT("FolioWorldVertices"), VertexBytes)
						.AddUsage(EBufferUsageFlags::Volatile)
						.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
				uint8* VertexData = (uint8*)RHICmdList.LockBuffer(VertexBuffer, 0, VertexBytes, RLM_WriteOnly);

				const uint32 IndexBytes = TotalIndices * sizeof(uint32);
				IndexBuffer = RHICmdList.CreateBuffer(
					FRHIBufferCreateDesc::CreateIndex(TEXT("FolioWorldIndices"), IndexBytes, sizeof(uint32))
						.AddUsage(EBufferUsageFlags::Volatile)
						.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
				uint8* IndexData = (uint8*)RHICmdList.LockBuffer(IndexBuffer, 0, IndexBytes, RLM_WriteOnly);

				for (const FFolioDraw& Draw : All)
				{
					const uint32 Vertices = Draw.Vertices.Num() * sizeof(FFolioUIVertex);
					FMemory::Memcpy(VertexData, Draw.Vertices.GetData(), Vertices);
					VertexData += Vertices;

					// Indices stay draw-local; BaseVertexIndex on the draw call offsets them.
					const uint32 Indices = Draw.Indices.Num() * sizeof(uint32);
					FMemory::Memcpy(IndexData, Draw.Indices.GetData(), Indices);
					IndexData += Indices;
				}

				RHICmdList.UnlockBuffer(VertexBuffer);
				RHICmdList.UnlockBuffer(IndexBuffer);
			}

			for (int32 DrawIndex = 0; DrawIndex < All.Num(); ++DrawIndex)
			{
				const FFolioDraw& Draw = All[DrawIndex];

				// A clip mask never reaches a recording, so anything that is neither geometry nor a
				// field has no meaning on this path.
				const bool bField = Draw.Kind == FFolioDraw::EKind::Field;
				if (!bField && Draw.Kind != FFolioDraw::EKind::Geometry)
				{
					continue;
				}

				if (Draw.Indices.IsEmpty() || Draw.Vertices.IsEmpty())
				{
					continue;
				}

				FGraphicsPipelineStateInitializer State;
				RHICmdList.ApplyCachedRenderTargets(State);
				State.PrimitiveType = PT_TriangleList;
				State.BoundShaderState.VertexDeclarationRHI =
					GFolioUIVertexDeclaration.VertexDeclarationRHI;
				State.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				State.BoundShaderState.PixelShaderRHI = bField
					? static_cast<FRHIPixelShader*>(FieldShader.GetPixelShader())
					: static_cast<FRHIPixelShader*>(PixelShader.GetPixelShader());

				// Two sided: a document is a plane, and which way it faces is the author's business
				// rather than something to punish with an invisible surface.
				State.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();

				// Premultiplied source over destination, matching RmlUi.
				State.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha,
					BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

				// In scene there is a depth buffer to test against, so the hardware rejects what is behind
				// geometry: reverse-Z, so nearer is greater. Never written, because a translucent surface
				// that wrote depth would occlude whatever came after it. After the tonemapper there is no
				// depth buffer bound at all, which is why occlusion is done in the shader there instead.
				State.DepthStencilState = bDepthTest
					? TStaticDepthStencilState<false, CF_GreaterEqual>::GetRHI()
					: TStaticDepthStencilState<false, CF_Always>::GetRHI();

				SetGraphicsPipelineState(RHICmdList, State, 0);

				FFolioWorldParameters Local = *Parameters;
				Local.UITexture = Draw.Texture.IsValid() && Draw.Texture->GetResource()
					? Draw.Texture->GetResource()->TextureRHI
					: GWhiteTexture->TextureRHI;
				Local.MsdfField = Draw.Field;
				Local.MsdfEffect = Draw.Effect;
				Local.UIOptions = FVector4f(Draw.bStraightAlpha ? 1.0f : 0.0f,
					Draws->ShouldDecodeSrgb() ? 0.0f : 1.0f, 0.0f, 0.0f);
				if (Draw.bTiles)
				{
					Local.UISampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				}

				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Local);
				if (bField)
				{
					SetShaderParameters(RHICmdList, FieldShader, FieldShader.GetPixelShader(), Local);
				}
				else
				{
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Local);
				}

				// After the pipeline state, every time: a stream bound before one is bound is dropped.
				RHICmdList.SetStreamSource(0, VertexBuffer, 0);
				RHICmdList.DrawIndexedPrimitive(IndexBuffer, BaseVertex[DrawIndex], 0,
					Draw.Vertices.Num(), FirstIndex[DrawIndex], Draw.Indices.Num() / 3, 1);
			}
		});
}

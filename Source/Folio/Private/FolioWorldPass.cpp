#include "FolioWorldPass.h"

#include "FolioDrawList.h"
#include "FolioModule.h"
#include "FolioWorldDocument.h"
#include "FolioWorldShader.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "SystemTextures.h"

namespace
{
	TAutoConsoleVariable<bool> CVarWorldDocuments(
		TEXT("folio.World"),
		true,
		TEXT("Draw world-space documents after the tonemapper, where temporal antialiasing cannot reach "
			"them. Off draws nothing, which is how to tell a Folio problem from a scene problem."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<bool> CVarWorldOcclusion(
		TEXT("folio.World.Occlusion"),
		true,
		TEXT("Reject fragments of a world-space document behind scene geometry. Off draws every document "
			"through the world, which separates an occlusion bug from a transform bug."),
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

void FFolioWorldPass::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Collected here because this is the last point on the game thread where a component can safely be
	// touched. Everything the pass needs is taken now, and nothing reads a UObject afterwards.
	Surfaces.Reset();

	for (UFolioWorldDocument* Document : UFolioWorldDocument::GetLive())
	{
		if (!Document || Document->DrawMode != EFolioWorldDrawMode::AfterTonemap)
		{
			continue;
		}

		if (Document->GetDrawList().IsEmpty())
		{
			continue;
		}

		FSurface& Surface = Surfaces.AddDefaulted_GetRef();
		Surface.Draws = &Document->GetDrawList();
		Surface.DocumentSize = Document->GetDocumentSize();
		Document->GetSurface(Surface.Centre, Surface.Right, Surface.Up);
	}
}

void FFolioWorldPass::SubscribeToPostProcessingPass(const EPostProcessingPass Pass, const FSceneView&,
	FPostProcessingPassDelegateArray& InOutPassCallbacks, const bool)
{
	// Tonemap is BL_SceneColorAfterTonemapping, which is after the temporal resolve. That is the whole
	// point: subscribing anywhere earlier would put the document back inside TAA.
	if (Pass != EPostProcessingPass::Tonemap || Surfaces.IsEmpty())
	{
		return;
	}

	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FFolioWorldPass::Draw));
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

	if (Surfaces.IsEmpty() || !SceneColour.IsValid())
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
	}
	else
	{
		Output = FScreenPassRenderTarget(SceneColour, ERenderTargetLoadAction::ELoad);
	}

	FRDGTextureRef SceneDepth = nullptr;
	if (Inputs.SceneTextures.SceneTextures)
	{
		SceneDepth = Inputs.SceneTextures.SceneTextures->GetContents()->SceneDepthTexture;
	}

	const bool bOcclude = SceneDepth != nullptr && CVarWorldOcclusion.GetValueOnRenderThread();

	// Scene depth is at render resolution while this target is at display resolution whenever an upscaler
	// runs, so a fragment's own pixel is not the same pixel in the depth buffer. Missing this is the
	// classic bug on this path: occlusion looks right at 100% screen percentage and wrong under TSR.
	FVector2f DepthScale(1.0f, 1.0f);
	if (bOcclude)
	{
		const FIntPoint DepthExtent = SceneDepth->Desc.Extent;
		const FIntPoint OutputExtent = Output.Texture->Desc.Extent;
		if (OutputExtent.X > 0 && OutputExtent.Y > 0)
		{
			DepthScale = FVector2f(
				float(DepthExtent.X) / float(OutputExtent.X),
				float(DepthExtent.Y) / float(OutputExtent.Y));
		}
	}

	const FMatrix ViewProjection = View.ViewMatrices.GetViewProjectionMatrix();
	const FIntRect Viewport = Output.ViewRect;

	for (const FSurface& Surface : Surfaces)
	{
		if (!Surface.Draws || Surface.Draws->IsEmpty())
		{
			continue;
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

		const FMatrix44f DocumentToClip = FMatrix44f(DocumentToWorld * ViewProjection);

		const TArray<FFolioDraw>& Draws = Surface.Draws->GetDraws();

		FFolioWorldParameters* Parameters = GraphBuilder.AllocParameters<FFolioWorldParameters>();
		Parameters->UISampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->UITexture = GWhiteTexture->TextureRHI;
		Parameters->DocumentToClip = DocumentToClip;
		Parameters->MsdfField = FVector4f::Zero();
		Parameters->MsdfEffect = FVector4f::Zero();
		Parameters->SceneDepthTexture = bOcclude
			? SceneDepth
			: GSystemTextures.GetBlackDummy(GraphBuilder);
		Parameters->DepthParams = FVector4f(DepthScale.X, DepthScale.Y, bOcclude ? 1.0f : 0.0f, 0.0f);
		Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		GraphBuilder.AddPass(RDG_EVENT_NAME("FolioWorldDocument"), Parameters, ERDGPassFlags::Raster,
			[Parameters, Viewport, &Draws](FRHICommandList& RHICmdList)
			{
				const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
				TShaderMapRef<FFolioWorldVS> VertexShader(ShaderMap);
				TShaderMapRef<FFolioWorldPS> PixelShader(ShaderMap);
				TShaderMapRef<FFolioWorldMsdfPS> FieldShader(ShaderMap);

				RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f,
					Viewport.Max.X, Viewport.Max.Y, 1.0f);

				for (const FFolioDraw& Draw : Draws)
				{
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

					// No depth buffer is bound at this point in the frame, which is exactly why occlusion
					// is done in the shader against scene depth instead.
					State.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

					SetGraphicsPipelineState(RHICmdList, State, 0);

					FFolioWorldParameters Local = *Parameters;
					Local.UITexture = Draw.Texture.IsValid() && Draw.Texture->GetResource()
						? Draw.Texture->GetResource()->TextureRHI
						: GWhiteTexture->TextureRHI;
					Local.MsdfField = Draw.Field;
					Local.MsdfEffect = Draw.Effect;

					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Local);
					if (bField)
					{
						SetShaderParameters(RHICmdList, FieldShader, FieldShader.GetPixelShader(), Local);
					}
					else
					{
						SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Local);
					}

					// Volatile buffers: a document's geometry is handed over whenever it is drawn, so
					// there is nothing worth keeping between frames.
					const uint32 VertexBytes = Draw.Vertices.Num() * sizeof(FFolioUIVertex);
					FBufferRHIRef VertexBuffer = RHICmdList.CreateBuffer(
						FRHIBufferCreateDesc::CreateVertex(TEXT("FolioWorldVertices"), VertexBytes)
							.AddUsage(EBufferUsageFlags::Volatile)
							.SetInitialState(ERHIAccess::VertexOrIndexBuffer));
					void* VertexData = RHICmdList.LockBuffer(VertexBuffer, 0, VertexBytes, RLM_WriteOnly);
					FMemory::Memcpy(VertexData, Draw.Vertices.GetData(), VertexBytes);
					RHICmdList.UnlockBuffer(VertexBuffer);

					const uint32 IndexBytes = Draw.Indices.Num() * sizeof(uint32);
					FBufferRHIRef IndexBuffer = RHICmdList.CreateBuffer(
						FRHIBufferCreateDesc::CreateIndex(TEXT("FolioWorldIndices"), IndexBytes,
								sizeof(uint32))
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
	}

	return MoveTemp(Output);
}

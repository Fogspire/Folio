#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SceneViewExtension.h"

/**
 * Draws world-space documents, by either of the two routes a document may ask for.
 *
 * **After tonemap** is the reason Phase 7 exists. Unreal's world-space UI looks poor mainly because TAA
 * and TSR blur and smear it, worst of all while the camera is moving, and no amount of resolution helps:
 * anything drawn in the scene goes through the temporal resolve. Subscribing to
 * `EPostProcessingPass::Tonemap` runs after it.
 *
 * What that buys, beyond the obvious:
 *
 * - Colour lands post-tonemap, so authored UI colours arrive as authored rather than being pushed around
 *   by the scene's exposure.
 * - Under an upscaler the target is at display resolution, so the UI is native-res over an upscaled scene.
 *
 * What it costs, and these are not oversights:
 *
 * - Bloom, depth of field and motion blur never reach the document.
 * - Occlusion is ours, by reading scene depth and rejecting fragments behind geometry.
 * - Edge antialiasing is ours, which is exactly what the distance fields provide analytically.
 *
 * **In scene** is the opposite trade, and never the default: a document opts in to being smeared, it does
 * not end up there by accident. Drawn into scene colour before post-processing, so the temporal resolve,
 * bloom, depth of field and colour grading all reach it - and so does TAA's smearing. Occlusion is the real
 * depth buffer rather than a comparison of ours, since at that point in the frame there is one to test
 * against. For a document that should sit *in* the world: a grimy monitor, lit by the scene.
 *
 * Both routes replay the same recording through the same shader. Only where they draw differs, which is
 * why they are one extension rather than two.
 */
class FFolioWorldPass : public FSceneViewExtensionBase
{
public:
	explicit FFolioWorldPass(const FAutoRegister& AutoRegister);

	//~ Begin FSceneViewExtensionBase
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {}

	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessingInputs& Inputs) override;

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End FSceneViewExtensionBase

private:
	/** One document's geometry and where it sits, copied for the render thread. */
	struct FSurface
	{
		TSharedPtr<const class FFolioDrawList> Draws;
		FVector Centre = FVector::ZeroVector;
		FVector Right = FVector::ZeroVector;
		FVector Up = FVector::ZeroVector;
		FIntPoint DocumentSize = FIntPoint(1, 1);
		bool bInScene = false;
	};

	/**
	 * Where a set of surfaces is being drawn, and what the shader has to correct for to land there.
	 *
	 * Bundled because the two routes differ in nothing else: same recording, same shader, same transform.
	 */
	struct FTarget
	{
		FRenderTargetBinding Colour;
		FDepthStencilBinding Depth;
		FIntRect Viewport;

		/** Read in the shader when there is no depth buffer to test against. Null on the in-scene path. */
		FRDGTextureRef DepthTexture = nullptr;
		int32 OcclusionMode = 0;
		FVector4f OutputRect = FVector4f::Zero();

		/** xy one over the target extent, z remap out of the view rect, w scale by pre-exposure. */
		FVector4f SceneTarget = FVector4f::Zero();

		/** True where the depth buffer is bound and does the rejecting itself. */
		bool bDepthTest = false;
	};

	FScreenPassTexture Draw(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Adds one surface's draws. Called per surface rather than per frame so each gets its own transform. */
	void AddSurface(FRDGBuilder& GraphBuilder, const FSceneView& View, const FSurface& Surface,
		const FMatrix& ViewProjection, const FTarget& Target) const;

	/** Whether any collected surface wants the given route, so a pass is only added when there is work. */
	bool HasSurfaces(bool bInScene) const;

	/**
	 * Collected on the game thread, read on the render thread.
	 *
	 * The draw lists are shared rather than copied: a document only re-records when it changes, so copying
	 * every frame would undo the invalidation work. Shared rather than pointed at, because a frame is still
	 * in flight when the next tick records, and a raw pointer into a list being rewritten is a race - one
	 * that shows up as geometry flickering while the camera moves.
	 */
	TArray<FSurface> Surfaces;
};

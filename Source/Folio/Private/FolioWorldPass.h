#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/**
 * Draws world-space documents after the tonemapper, where temporal antialiasing cannot reach them.
 *
 * This is the reason Phase 7 exists. Unreal's world-space UI looks poor mainly because TAA and TSR blur
 * and smear it, worst of all while the camera is moving, and no amount of resolution helps: anything
 * drawn in the scene goes through the temporal resolve. Subscribing to `EPostProcessingPass::Tonemap`
 * runs after it.
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

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End FSceneViewExtensionBase

private:
	/** One document's geometry and where it sits, copied for the render thread. */
	struct FSurface
	{
		const class FFolioDrawList* Draws = nullptr;
		FVector Centre = FVector::ZeroVector;
		FVector Right = FVector::ZeroVector;
		FVector Up = FVector::ZeroVector;
		FIntPoint DocumentSize = FIntPoint(1, 1);
	};

	FScreenPassTexture Draw(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/**
	 * Collected on the game thread, read on the render thread.
	 *
	 * The draw lists are referenced rather than copied: a document only re-records when it changes, so
	 * copying every frame would undo the invalidation work. Safe because the component cannot be
	 * destroyed between BeginRenderViewFamily and the pass, both of which are inside one frame.
	 */
	TArray<FSurface> Surfaces;
};

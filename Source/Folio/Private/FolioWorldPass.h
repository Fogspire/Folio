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

	// The mobile renderer never calls the hook above; it has its own, differing only in which scene
	// texture uniform buffer it carries. Without this an in-scene document draws nothing at all under
	// mobile forward, silently, while the after-tonemap route keeps working.
	virtual void PrePostProcessPassMobile_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FMobilePostProcessingInputs& Inputs) override;

	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End FSceneViewExtensionBase

	/**
	 * Drops these recordings from what the render thread is drawing.
	 *
	 * The snapshot is only replaced when a family renders with a live document in it, so a document torn down
	 * with its world (the end of PIE) would otherwise stay in the last one for good, holding its textures and
	 * through them the old world. Compared by address only; the caller still owns the lists.
	 */
	void ForgetDrawLists(TArray<const class FFolioDrawList*> Lists);

private:
	/** The in-scene route, once the renderer's own scene colour and depth have been found. */
	void DrawInScene(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SceneColour,
		FRDGTextureRef SceneDepth);

	/** One document's geometry and where it sits, copied for the render thread. */
	struct FSurface
	{
		TSharedPtr<const class FFolioDrawList> Draws;
		FVector Centre = FVector::ZeroVector;
		FVector Right = FVector::ZeroVector;
		FVector Up = FVector::ZeroVector;
		FIntPoint DocumentSize = FIntPoint(1, 1);
		bool bInScene = false;

		/** After tonemap only: this surface skips the occlusion test. See UFolioWorldDocument::bAlwaysOnTop. */
		bool bAlwaysOnTop = false;
		/** See UFolioWorldDocument::bBehindTranslucency. */
		bool bBehindTranslucency = false;
	};

	/**
	 * One separate translucency layer as the renderer composites it: scene * colour.a * modulate + colour.rgb.
	 *
	 * Both textures are carried because alpha alone is not the whole story. Additive translucency leaves it at
	 * one and lives entirely in the colour, and a modulated layer lives in the modulate texture. A layer the
	 * view does not have is left null, and the shader is given dummies that composite to nothing.
	 */
	struct FTranslucencyLayer
	{
		FRDGTextureRef Colour = nullptr;
		FRDGTextureRef Modulate = nullptr;

		/** xy where the view starts in those textures, zw its size, as fractions of their extent. */
		FVector4f Rect = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
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

		/**
		 * After tonemap only: the separate translucency a document is placed behind, in the order the renderer
		 * composites it. Left null where there is nothing to fade by, and the shader is given dummies that
		 * leave the document alone.
		 */
		FTranslucencyLayer AfterDof;
		FTranslucencyLayer AfterMotionBlur;

		/**
		 * After tonemap only: the scene colour from before either layer was composited into it, and the
		 * tonemapped scene colour this document is drawn over. Set with the layers or not at all. The first is
		 * what the layers were composited over and the second is what the viewer sees, so between them they
		 * say how much of the picture under a document is translucency's own glow. Null where there is nothing
		 * to fade by, and the shader is given black.
		 */
		FRDGTextureRef SceneColourBefore = nullptr;
		FRDGTextureRef SceneColourAfter = nullptr;

		/** xy where the view starts in SceneColourAfter, zw its size, as fractions of the texture's extent. */
		FVector4f SceneColourAfterRect = FVector4f(0.0f, 0.0f, 1.0f, 1.0f);
	};

	FScreenPassTexture Draw(FRDGBuilder& GraphBuilder, const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Adds one surface's draws. Called per surface rather than per frame so each gets its own transform. */
	void AddSurface(FRDGBuilder& GraphBuilder, const FSceneView& View, const FSurface& Surface,
		const FMatrix& ViewProjection, const FTarget& Target) const;

	/** Whether any collected surface wants the given route, so a pass is only added when there is work. */
	bool HasSurfaces(bool bInScene) const;

	/**
	 * What the render thread is drawing. Never touched from the game thread.
	 *
	 * A whole snapshot is handed over per frame rather than one array being refilled in place. The render
	 * thread is a frame behind, so the game thread collecting frame N+1 into an array the render thread is
	 * still iterating for frame N reallocates it mid-loop: the surface reference the pass is holding
	 * dangles, and the frame crashes somewhere inside the draw with nothing pointing back here.
	 *
	 * The draw lists inside it are shared rather than copied: a document only re-records when it changes,
	 * so copying every frame would undo the invalidation work.
	 */
	TSharedPtr<const TArray<FSurface>> RenderSurfaces;

	/**
	 * The after-DOF and after-motion-blur translucency one view produced, kept from where it is reachable to
	 * where it is read.
	 */
	struct FViewTranslucency
	{
		const FSceneView* View = nullptr;
		uint32 FrameNumber = 0;
		FTranslucencyLayer AfterDof;
		FTranslucencyLayer AfterMotionBlur;
	};

	/**
	 * Render thread only. PrePostProcessPass is the only hook that is handed the translucency textures, and
	 * the tonemap callback that wants them runs later in the same graph, so this carries them across.
	 *
	 * Keyed by frame number as well as view, and a view's entry is cleared each time it passes through the
	 * hook above: a texture belongs to one frame's graph, so a view that never reaches the tonemapper must
	 * not leave a dangling RDG reference for a later frame to pick up, nor one for another family in the
	 * same frame.
	 */
	TArray<FViewTranslucency> ViewTranslucency;
};

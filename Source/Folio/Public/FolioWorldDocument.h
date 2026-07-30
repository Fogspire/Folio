#pragma once

#include "Components/SceneComponent.h"
#include "FolioDrawList.h"
#include "CoreMinimal.h"

#include "FolioWorldDocument.generated.h"

class FFolioDocumentHost;

/**
 * How a world-space document reaches the screen.
 *
 * The choice is a genuine trade with no right answer, so it belongs to the game rather than to Folio.
 */
UENUM(BlueprintType)
enum class EFolioWorldDrawMode : uint8
{
	/**
	 * Drawn after the tonemapper, where temporal antialiasing can no longer touch it.
	 *
	 * The default, and the reason this exists. TAA and TSR blur and smear world-space UI, worst of all
	 * while the camera moves, and no amount of resolution fixes it. Drawing after the temporal resolve
	 * avoids it outright. Colour also lands post-tonemap, so authored UI colours arrive as authored, and
	 * under an upscaler the target is at display resolution rather than render resolution.
	 *
	 * The price: bloom, depth of field and motion blur never reach it, and the edges are ours to
	 * produce - which is what the distance fields already do analytically.
	 */
	AfterTonemap,

	/**
	 * Drawn into the scene as world-space triangles.
	 *
	 * Sharp at any distance because there is no render target and no resample, and it receives bloom,
	 * depth of field and colour grading like anything else in the world. Temporal antialiasing will
	 * smear it. For a diegetic screen that should sit *in* the world - a grimy monitor lit by the scene.
	 */
	InScene,

	/**
	 * Rendered into an offscreen target for a material on a mesh to sample.
	 *
	 * The only mode with layer effects: box-shadow, filter, backdrop-filter and mask-image all need an
	 * offscreen target, and the whole document composites as one translucent surface. Cheaper here than it
	 * would be for UMG, because geometry is compiled once and glyphs are size-independent, so re-rendering
	 * at a new resolution costs no atlas work.
	 *
	 * The price is the one resample the other two modes exist to avoid, which is why this is not the
	 * default. Folio does not own the mesh: assign GetRenderTarget to a material and the mesh is the game's.
	 */
	Surface,
};

/**
 * A document on a surface in the world: a terminal, a sign, a screen you can walk up to.
 *
 * Sharpness at any distance is the requirement that decides the design, which is why this is not a
 * `UWidgetComponent`. That renders into a fixed-size render target and samples the quad at whatever
 * size it lands on screen, so every mismatch is a filtered resample. Distance fields make the
 * alternative possible: geometry drawn at the size it is seen, with no intermediate target.
 *
 * This component owns the document and reports where it is. It does not draw: the draw mode decides
 * which path picks it up, and both read the same recorded geometry.
 */
UCLASS(ClassGroup = (Folio), meta = (BlueprintSpawnableComponent, DisplayName = "Folio World Document"))
class FOLIO_API UFolioWorldDocument : public USceneComponent
{
	GENERATED_BODY()

public:
	UFolioWorldDocument();

	/**
	 * Document to load, relative to the project directory.
	 *
	 * A leading /folio/ resolves inside the plugin instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FString DocumentPath;

	/**
	 * The physical size of the surface in world units, before the component's scale.
	 *
	 * The document is laid out to this size times PixelsPerUnit, so a wider surface shows more of the
	 * document rather than the same content stretched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D WorldSize = FVector2D(100.0, 60.0);

	/**
	 * Document pixels per world unit, which is what decides how much fits on the surface.
	 *
	 * Not a quality setting: glyphs and vector art are resolution-independent, so this does not affect
	 * sharpness at all. It is the authoring scale. At the default a 100x60 surface is a 1000x600
	 * document, so 16dp text reads at about the size it would on a screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0.1"))
	float PixelsPerUnit = 10.0f;

	/**
	 * Whether temporal antialiasing may touch this document, and what reaches it. See the enum.
	 *
	 * Initialised from the project's DefaultWorldDrawMode, under Project Settings > Plugins > Folio.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	EFolioWorldDrawMode DrawMode = EFolioWorldDrawMode::AfterTonemap;

	/**
	 * Surface mode only: the largest the offscreen target may be, as a multiple of the document's size.
	 *
	 * The target is sized from how large the surface lands on screen, so walking up to it sharpens it
	 * rather than magnifying a fixed texture. This bounds what that can cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0.25", ClampMax = "4.0"))
	float MaxSurfaceScale = 2.0f;

	/** Whether the document updates and draws in the level viewport, rather than only in play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta=(Keywords="tick"))
	bool bUpdateInEditor = true;

	/** Loads a different document, replacing whatever is up. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void LoadDocument(const FString& InDocumentPath);

	/**
	 * Binds a UObject's properties as a named data model, so the document can read them.
	 *
	 * Same contract as the screen-space widget: callable before the component is registered, and the
	 * document's bindings resolve against whatever is bound when it loads.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	bool BindViewModel(FName ModelName, UObject* Source, bool bPollEveryFrame = true);

	/** Says the document changed, for game code that reaches into it directly. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void MarkDirty();

	/** Rebuilds from disk, putting focus and scroll back. What a markup change needs. */
	void ReloadDocument();

	/** Re-reads stylesheets, leaving the DOM, focus, scroll and data models alone. */
	void ReloadStyleSheets();

	/** The document's size in pixels, which is WorldSize times PixelsPerUnit. */
	FIntPoint GetDocumentSize() const;

	/**
	 * The surface in world space: origin at its centre, extents along its local X and Y.
	 *
	 * The document's own coordinates run from the top left, so the plane is expressed the way a
	 * renderer wants it rather than the way the component is authored.
	 */
	void GetSurface(FVector& OutCentre, FVector& OutRight, FVector& OutUp) const;

	FFolioDocumentHost* GetHost() const { return Host.Get(); }

	/**
	 * The document's geometry as of the last update, in document space.
	 *
	 * Recorded rather than drawn, because where it lands depends on which pass picks it up. Null until
	 * the first update produces something.
	 *
	 * Shared and immutable: a render pass reads this for as long as its frame is in flight, which
	 * outlasts the tick that handed it over. Re-recording publishes a new list rather than rewriting
	 * this one, so nothing is ever read while it is being built.
	 */
	TSharedPtr<const FFolioDrawList> GetDrawList() const { return Published; }

	/**
	 * Surface mode only: what the document was last rendered into, for a material to sample.
	 *
	 * Null in the other modes, and until the first update produces something. The target is replaced rather
	 * than resized when the resolution changes, so a material holding it needs re-assigning: bind it from
	 * OnSurfaceChanged rather than once at BeginPlay.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	class UTextureRenderTarget2D* GetRenderTarget() const { return SurfaceTarget.Get(); }

	/** Fires when GetRenderTarget starts returning a different target, which a resolution change causes. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSurfaceChanged, UTextureRenderTarget2D*, Target);
	UPROPERTY(BlueprintAssignable, Category = "Folio")
	FOnSurfaceChanged OnSurfaceChanged;

	/** Every live world document, so a render pass can find them without walking the world. */
	static const TArray<UFolioWorldDocument*>& GetLive();

	//~ Begin UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** One requested binding, kept so it survives a re-register the way the widget's do. */
	struct FPendingBinding
	{
		FName ModelName;
		TWeakObjectPtr<UObject> Source;
		bool bPollEveryFrame = true;
	};

	void ApplyBindings();

	/**
	 * Drops the recordings, waiting first if a frame is still reading one.
	 *
	 * The wait is the point: a recorded draw holds a TStrongObjectPtr, and letting a render worker release
	 * the last reference would touch the garbage collector's reference set from the wrong thread.
	 */
	void ReleaseRecordings();

	/** Renders the document into an offscreen target of the given size. Surface mode only. */
	void RenderToSurface(FIntPoint Size);

	/**
	 * How large the offscreen target should be, from how large the surface lands on screen.
	 *
	 * Quantised, because a target that changed size every frame would reallocate every frame and the
	 * document would re-render at a new size continuously. Falls back to the document's own size where
	 * there is no view to measure against, which includes the level viewport.
	 */
	FIntPoint DesiredSurfaceSize() const;

	/** Held by pointer so the host stays private to the module while this header is public. */
	TUniquePtr<FFolioDocumentHost> Host;

	TArray<FPendingBinding> Bindings;

	/**
	 * Re-recorded only when the document actually updated.
	 *
	 * A settled document keeps the recording it already has, which is the same reason the screen path
	 * lets Slate keep its cached draw elements.
	 */
	TSharedPtr<const FFolioDrawList> Published;

	/** Surface mode's offscreen target, and the size it was made at. */
	TStrongObjectPtr<class UTextureRenderTarget2D> SurfaceTarget;
	FIntPoint SurfaceSize = FIntPoint::ZeroValue;

	/**
	 * Superseded lists a frame in flight may still be reading, dropped here once nothing else holds one.
	 *
	 * Released on the game thread deliberately: a recorded draw holds a TStrongObjectPtr, and letting the
	 * last reference go on a render worker would touch the garbage collector's reference set from the
	 * wrong thread.
	 */
	TArray<TSharedPtr<const FFolioDrawList>> Retired;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> BoundSources;
};

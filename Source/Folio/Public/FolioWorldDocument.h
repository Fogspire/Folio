#pragma once

#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FolioDocumentPath.h"

#include "CoreMinimal.h"

#include "FolioWorldDocument.generated.h"

class FFolioDocumentHost;

// A recording is passed around by shared pointer and never opened here. Only the render pass reads
// one, and it has the header.
class FFolioDrawList;

namespace Rml
{
	class Element;
}

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
	 * default. Folio does not own the mesh: instance /Folio/M_FolioSurface, feed its Element parameter
	 * from OnSurfaceChanged, and the mesh is the game's. See docs/world_documents.md.
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
	// WorldSize is in world units, not pixels, so the declared size is divided by PixelsPerUnit. Set
	// that first: assigning a document reads it as it stands.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio",
		meta = (FolioSizeProperty = "WorldSize", FolioSizeDivisor = "PixelsPerUnit"))
	FFolioDocumentPath DocumentPath;

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

	/** After tonemap only: skips the occlusion test, so scene geometry never hides this document. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	bool bAlwaysOnTop = false;

	/**
	 * After tonemap only: fades the document under translucency in front of it (fire, smoke, additive glow)
	 * rather than drawing crisply over it. Applies with bAlwaysOnTop too.
	 *
	 * Deferred renderer only, and only for translucency in the After DOF or After Motion Blur pass, unless
	 * r.Translucency.StandardSeparated is on. Costs a scene colour copy and six texture reads per pixel.
	 * See docs/world_documents.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	bool bBehindTranslucency = false;

	/**
	 * After tonemap only: drawn after the after-tonemap post process materials rather than before them, so a
	 * fade or vignette made by one never covers this document. For a menu over the game, not for a screen that
	 * belongs in the world.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	bool bAfterPostProcessMaterials = false;

	/**
	 * Surface mode only: the largest the offscreen target may be, as a multiple of the document's size.
	 *
	 * The target is sized from how large the surface lands on screen, so walking up to it sharpens it
	 * rather than magnifying a fixed texture. This bounds what that can cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio",
		meta = (ClampMin = "0.25", ClampMax = "4.0",
			EditCondition = "DrawMode == EFolioWorldDrawMode::Surface", EditConditionHides))
	float MaxSurfaceScale = 2.0f;

	/** Whether the document updates and draws in the level viewport, rather than only in play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta=(Keywords="tick"))
	bool bUpdateInEditor = true;

	/**
	 * Drives the pointer from where the local player is looking, with no input wiring at all.
	 *
	 * For trying a document out and for a game that aims with the camera. Anything else - a cursor, a
	 * touch, a hand in VR - should leave this off and call MovePointerAlong itself, which is the same
	 * path with the ray supplied.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input")
	bool bPointerFromPlayerView = false;

	/**
	 * Traces against the mesh and uses its UVs, rather than intersecting the component's rectangle.
	 *
	 * What a curved or inset screen needs, and what a flat quad does not. See MovePointerToHit for the
	 * project settings it depends on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input",
		meta = (EditCondition = "bPointerFromPlayerView", EditConditionHides))
	bool bPointerUsesMeshUV = false;

	/** Which channel the mesh trace runs on, when the pointer comes from the mesh's UVs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input",
		meta = (EditCondition = "bPointerFromPlayerView && bPointerUsesMeshUV", EditConditionHides))
	TEnumAsByte<ECollisionChannel> PointerTraceChannel = ECC_Visibility;

	/**
	 * Which UV channel the screen is mapped on.
	 *
	 * Only matters when pointing by mesh UVs, and only when the mesh has more than one: the material
	 * samples a channel of its own choosing, and reading a different one here points somewhere else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input",
		meta = (EditCondition = "bPointerUsesMeshUV", EditConditionHides, ClampMin = "0"))
	int32 PointerUVChannel = 0;

	/**
	 * Flips the axes the pointer is derived on, for a mesh whose UVs run the other way.
	 *
	 * The symptom is a cursor that moves the wrong way while the picture looks right, which happens when
	 * the mesh was authored with V running up or the material corrects a flip the pointer does not see.
	 * Applies to the derived routes, not to Move Pointer, which is given a UV outright.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input")
	bool bInvertPointerV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input")
	bool bInvertPointerU = false;

	/** How far the player can be from the surface and still point at it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input",
		meta = (EditCondition = "bPointerFromPlayerView", EditConditionHides, ClampMin = "1.0",
			ForceUnits = "cm"))
	float PointerReach = 400.0f;

	/**
	 * Draws a cursor on the surface at the pointer.
	 *
	 * A screen in the world has no system cursor and the player is aiming a camera at it, so without one
	 * there is nothing saying where a click will land until something highlights. Added to the document
	 * from here rather than authored, so it works on any document and takes no markup.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input")
	bool bShowCursor = true;

	/** What counts as a click while the pointer comes from the player's view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Input",
		meta = (EditCondition = "bPointerFromPlayerView", EditConditionHides))
	FKey PointerKey;

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
	bool BindViewModel(const FString& ModelName, UObject* Source, bool bPollEveryFrame = true);

	/** Says the document changed, for game code that reaches into it directly. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void MarkDirty();

	/**
	 * Puts the pointer at a point on the surface, in 0-1 with the origin at the top left.
	 *
	 * The primitive the rest of the input surface is built on. Use it directly when the surface is not
	 * flat and the UV comes from somewhere Folio cannot work out, such as FindCollisionUV on a hit
	 * against a curved mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void MovePointer(FVector2D SurfaceUV);

	/**
	 * Puts the pointer where a line from Origin along Direction crosses the component's rectangle.
	 *
	 * Returns false and leaves the pointer alone if the line misses, so the same call decides both
	 * whether the document is being looked at and where. Works whatever the mesh is, because it uses
	 * the component's own plane rather than the geometry: place the component over the screen face with
	 * WorldSize matching it and a modelled bezel, curve or oval cutout costs nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	bool MovePointerAlong(FVector Origin, FVector Direction, float Distance = 1000.0f);

	/**
	 * Puts the pointer where a line trace hit the mesh, using the mesh's own UVs.
	 *
	 * What a curved or inset screen needs. MovePointerAlong intersects a flat rectangle, which agrees
	 * with a domed tube face only at its centre and drifts further off towards the edges, so a CRT reads
	 * as accurate in the middle and wrong everywhere else.
	 *
	 * Needs Support UV From Hit Results in Project Settings > Physics, complex collision on the mesh, and
	 * a trace that asked for complex geometry. Returns false and says so in the log if the UV is
	 * unavailable, rather than pointing somewhere wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	bool MovePointerToHit(const FHitResult& Hit);

	/** Takes the pointer off the document, so nothing is left hovered. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void ClearPointer();

	/** A press at wherever the pointer is. Left, right and middle mouse buttons. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void PressPointer(FKey Button);

	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void ReleasePointer(FKey Button);

	/** Scrolls under the pointer. Positive is down, matching a wheel turned towards the user. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void ScrollPointer(float Lines);

	/**
	 * Whether a text field in the document has focus, which is when typing goes to it.
	 *
	 * While this is true the document holds Slate's keyboard focus, so characters reach it with the
	 * layout, modifiers and IME Slate already resolved. Keys the document does not consume still fall
	 * through to the game.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	bool IsTakingText() const;

	/**
	 * Hands input to this document: the d-pad, the arrows, WASD and IJKL all navigate it.
	 *
	 * What turns a screen in the world into the thing the player is using. Directional input the
	 * document acts on stops reaching the game, so the player navigates the terminal rather than walking
	 * while they do it. Anything Folio has no meaning for still falls through, and Escape and the
	 * console key always do.
	 *
	 * Focus is one document at a time. Focusing another releases this one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void FocusDocument();

	/** Whether this document currently has input. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Folio|Input")
	bool IsFocused() const { return bFocusRequested; }

	/** Gives input back to the game, and blurs whatever the document had focused. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	void ReleaseFocus();

	/**
	 * Puts the focus ring on one named element.
	 *
	 * Focusing the document hands it directional input, but RmlUi navigates from whatever element is focused
	 * inside it, and a document that has never focused one has nothing to navigate from. A list has to say which
	 * row it starts on, and has to say it again every time the rows are replaced.
	 *
	 * Returns false if the id names nothing, or names something that cannot take focus.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	bool FocusElement(const FString& ElementId, bool bShowRing = true);

	/**
	 * What a form control in the document is currently set to, as it would be submitted.
	 *
	 * For a control the game drives rather than binds: a data binding into an array is mirrored, so a control
	 * standing for one row of a list cannot write back through one.
	 *
	 * Empty if the id names nothing, or names something that is not a form control.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Input")
	FString GetElementValue(const FString& ElementId) const;

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

	/**
	 * How tall this surface lands on screen right now, in pixels, or 0 with no view to measure against.
	 *
	 * What Surface mode sizes its target from, and the number to compare the target against when a
	 * surface looks soft. A target already at or above this is not what is limiting it - the scene's
	 * own rendering resolution is, since the mesh is scene geometry and goes through whatever upscaler
	 * and temporal antialiasing the project runs.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	int32 GetScreenHeightPixels() const;

	/**
	 * The same measurement against a camera given rather than found, for the editor's viewport.
	 *
	 * There is no player camera while authoring, and the whole point of the button that uses this is to
	 * measure against the view the author has arranged.
	 */
	int32 ComputeScreenHeightPixels(const FVector& CameraLocation, float FovDegrees,
		int32 ViewportHeight) const;

	/**
	 * Picks the density that lays the document out at roughly one document pixel per screen pixel.
	 *
	 * The only estimate that means anything: a document laid out much larger than the panel covers on
	 * screen is minified, so its text is drawn at a fraction of the size it was authored at, and no
	 * amount of target resolution makes two-pixel-tall text readable. What this cannot know is the
	 * distance intended, which is why it takes one rather than guessing.
	 *
	 * Returns false when there is nothing sensible to set, which is a surface of no height or a screen
	 * measurement of zero.
	 */
	bool SetPixelsPerUnitForScreenHeight(int32 ScreenHeightPixels);

	/** Fires when GetRenderTarget starts returning a different target, which a resolution change causes. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSurfaceChanged, UTextureRenderTarget2D*, Target);
	UPROPERTY(BlueprintAssignable, Category = "Folio")
	FOnSurfaceChanged OnSurfaceChanged;

	/**
	 * The pad's back button while the document is focused, when nothing in the document consumed it.
	 *
	 * Escape is not reported here: it always falls through to the game, so the game's own binding for it still fires.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWorldBack);
	UPROPERTY(BlueprintAssignable, Category = "Folio|Input")
	FOnWorldBack OnBack;

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

	/**
	 * Reports a document path that names nothing, and a placed component with no document at all.
	 *
	 * Both render nothing, which is indistinguishable from a document that loaded and is laid out
	 * wrong, and the mistake would otherwise surface the first time the level is played.
	 */
	virtual void CheckForErrors() override;
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	/** One requested binding, kept so it survives a re-register the way the widget's do. */
	struct FPendingBinding
	{
		FString ModelName;
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
	 * Fills the surface target's mip chain, which nothing else does on this path.
	 *
	 * The document is drawn through RDG rather than a canvas, so the engine's automatic generation never
	 * runs and the chain stays empty. An empty chain is worse than none: the material samples trilinear
	 * and reads uninitialised mips. Cheap in practice, because a settled document does not re-render.
	 */
	void GenerateSurfaceMips();

	/**
	 * How large the offscreen target should be, from how large the surface lands on screen.
	 *
	 * Quantised, because a target that changed size every frame would reallocate every frame and the
	 * document would re-render at a new size continuously. Falls back to the document's own size where
	 * there is no view to measure against, which includes the level viewport.
	 */
	FIntPoint DesiredSurfaceSize() const;

	/** Traces from the local player's view and drives the pointer and its button. */
	void TickPlayerPointer();

	/** Line traces the owner's mesh and points at the UV that was hit. */
	bool TracePointerToMesh(FVector Origin, FVector Direction);

	/** The invert switches, applied to a UV this worked out rather than to one it was given. */
	FVector2D ApplyPointerFlips(FVector2D UV) const;

	/** Draws the rectangle the pointer resolves against, for folio.World.ShowSurface. */
	void DrawDebugSurface() const;

	/** The element under the pointer, as tag#id. What says whether a click landed where it looked. */
	FString DescribeHover() const;

	/** Moves the on-surface cursor, making it if the document does not have one yet. */
	void UpdateCursor();

	/**
	 * The cursor element, owned by the document rather than by this.
	 *
	 * Cleared when the document closes, because by then the element is freed and touching it is a
	 * use-after-free rather than a no-op.
	 */
	Rml::Element* Cursor = nullptr;

	/** What the cursor was last moved to, so a settled document is not dirtied every frame. */
	TOptional<FIntPoint> CursorAt;

	/** Takes or hands back Slate's keyboard focus, following whether a text field has it. */
	void UpdateKeyboardFocus();

	/** Held by pointer so the host stays private to the module while this header is public. */
	// Shared rather than unique, because this header only forward declares the host: a TSharedPtr
	// captures its deleter where the type is complete, so no consumer of this header has to be. With a
	// TUniquePtr it compiles only while every consumer happens to have seen the definition first, which
	// is a property of the unity-build grouping rather than of any file.
	TSharedPtr<FFolioDocumentHost> Host;

	/**
	 * Where the pointer is, in document pixels, or unset when it is off the surface.
	 *
	 * Kept because RmlUi resolves a click against whatever the last move hovered, so a press has to be
	 * preceded by a move even when nothing has moved.
	 */
	TOptional<FIntPoint> Pointer;

	/** Why the last ray did not land on the surface, for folio.World.ShowSurface to report. */
	enum class EPointerMiss : uint8
	{
		Landed,
		NotDriven,
		Parallel,
		Behind,
		TooFar,
		Outside,
		NoMesh,
		NoMeshUv,
	};

	EPointerMiss LastMiss = EPointerMiss::NotDriven;

	/** What was last written to the log, so a steady state is not repeated every frame. */
	mutable EPointerMiss LoggedMiss = EPointerMiss::Landed;
	mutable double LoggedPointerAt = 0.0;

	/** Whether PointerKey was down last tick, so the auto pointer sends edges rather than a state. */
	bool bPointerKeyHeld = false;

	/**
	 * Which buttons are down, by RmlUi's index. A drag lasts from a press to its release.
	 *
	 * A pointer that wanders off the surface mid-drag must not take the drag with it, which is the same
	 * reason the screen canvas captures the mouse on a press.
	 */
	int32 HeldButtons = 0;

	/** Whether this document is the one currently holding Slate's keyboard focus. */
	bool bHoldingFocus = false;

	/** Whether the game asked for input outright, rather than a text field having pulled it in. */
	bool bFocusRequested = false;

	/** What the sink was last told, so a change between the two levels reaches it. */
	bool bLastNavigating = false;

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

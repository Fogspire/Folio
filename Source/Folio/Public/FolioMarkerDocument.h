#pragma once

#include "Components/SceneComponent.h"
#include "FolioDocumentPath.h"

#include "CoreMinimal.h"

#include "FolioMarkerDocument.generated.h"

class FFolioDocumentHost;

/**
 * How a marker leaves the screen when it runs out of distance.
 *
 * A marker that vanishes between one frame and the next reads as a bug, and a screen with thirty of
 * them popping in and out reads as noise. This is what it does across the last stretch instead.
 */
UENUM(BlueprintType)
enum class EFolioMarkerTransition : uint8
{
	/** Drawn or not drawn, with nothing in between. */
	None,

	/** Opacity, which is what a marker did before there was a choice. */
	Fade,

	/** Shrinks towards its anchor, so it recedes rather than dissolving. */
	Scale,

	/** Both. It reads as the marker going away rather than being switched off, which is the point. */
	FadeAndScale,
};

/**
 * What a marker measures its distance from, for scaling and fading.
 *
 * The two part company the moment a camera is not on the player: a third-person camera pulled back
 * from the shoulder, a camera that swings wide in combat, an aim mode that pushes in. A marker banded
 * off the camera then changes size and fades as the *camera* moves, which reads as the world
 * breathing rather than as the player closing on something.
 */
UENUM(BlueprintType)
enum class EFolioMarkerDistanceFrom : uint8
{
	/** The view. Right when the bands are about how much of the screen a marker is worth. */
	Camera,

	/**
	 * The player's pawn, falling back to the camera when there is not one.
	 *
	 * Right when the bands are about the world - how far away the thing being labelled actually is,
	 * which is what a player means by near and far.
	 */
	Pawn,
};

/**
 * A document that lives on the screen but follows a place in the world.
 *
 * Nameplates, objective markers, damage numbers, interaction prompts, waypoints. The document is laid
 * out and drawn in screen space, so it is always square to the camera and always crisp, and only its
 * *position* comes from the world. It does not occlude and cannot be occluded - a marker behind a wall
 * still draws, which is usually what a marker is for. Hiding one is the game's decision, and
 * SetVisibility is how to make it.
 *
 * This is the other half of UFolioWorldDocument, which puts a document *in* the world - a monitor on a
 * desk, a sign on a wall - where perspective and occlusion are the point. If the thing should look
 * like it is in the room, use that. If it should look like it is on the HUD, use this.
 *
 * Nothing here is exotic: project a world point to the screen, place the document there, scale it by
 * distance. It exists so that every game does not write the projection, the culling, the scale curve
 * and the fade again, and get the DPI conversion subtly wrong each time.
 */
UCLASS(ClassGroup = (Folio), meta = (BlueprintSpawnableComponent, DisplayName = "Folio Marker Document"))
class FOLIO_API UFolioMarkerDocument : public USceneComponent
{
	GENERATED_BODY()

public:
	UFolioMarkerDocument();

	/**
	 * Document to load, relative to the project directory.
	 *
	 * A leading /folio/ resolves inside the plugin instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio",
		meta = (FolioSizeProperty = "Size"))
	FFolioDocumentPath DocumentPath;

	/** The document's size in dp. It is laid out at this size wherever it ends up on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D Size = FVector2D(256.0, 64.0);

	/**
	 * Which point of the document sits on the projected location, as a fraction of its size.
	 *
	 * (0.5, 1) is the default and the right answer for a nameplate: the bottom centre, so the plate
	 * sits above the point rather than covering it. (0.5, 0.5) centres it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D Anchor = FVector2D(0.5, 1.0);

	/** Moved by this many dp after projection, so a plate can clear a head without moving the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D ScreenOffset = FVector2D(0.0, -16.0);

	/**
	 * Where every distance below is measured from: the view, or the player standing in the world.
	 *
	 * Camera by default, which is what a marker did before there was a choice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance")
	EFolioMarkerDistanceFrom DistanceFrom = EFolioMarkerDistanceFrom::Camera;

	/**
	 * Shrink with distance, the way something in the world would.
	 *
	 * Off by default. A marker that stays one size is easier to read and is what most HUDs want; a
	 * nameplate that should feel attached to a body wants this on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance")
	bool bScaleWithDistance = false;

	/** Distance in world units at which the document draws at its authored size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "bScaleWithDistance", EditConditionHides, ClampMin = "1.0",
			ForceUnits = "cm"))
	float ReferenceDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "bScaleWithDistance", EditConditionHides, ClampMin = "0.01"))
	float MinScale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "bScaleWithDistance", EditConditionHides, ClampMin = "0.01"))
	float MaxScale = 1.5f;

	/**
	 * Goes away between these distances, and stops drawing past the second.
	 *
	 * Zero for both switches it off. Thinning by distance matters more than it looks: a screen with
	 * thirty markers on it reads as noise, and this is the cheapest way to have fewer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float FadeStartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float FadeEndDistance = 0.0f;

	/** What it does across that stretch. Fade is what it did before this was a choice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "FadeEndDistance > 0.0", EditConditionHides))
	EFolioMarkerTransition DistanceTransition = EFolioMarkerTransition::Fade;

	/**
	 * Keeps an off-screen marker at the edge of the screen, pointing at where it is.
	 *
	 * Off by default, because it is right for objectives and wrong for nameplates: a clamped nameplate
	 * is a label stuck to the side of the screen with no body under it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Screen Edge")
	bool bClampToScreenEdge = false;

	/** How far in dp a clamped marker sits from the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Screen Edge",
		meta = (EditCondition = "bClampToScreenEdge", EditConditionHides, ClampMin = "0.0"))
	float ClampMargin = 48.0f;

	/**
	 * How far away a marker may be and still be clamped, in world units. Zero is no limit.
	 *
	 * Clamping is what stops the player losing an objective they cannot see. It is not what should
	 * happen to every marker in the level: a rim of arrows for things a mile behind them is noise,
	 * and the far ones are the least useful. Past `Clamp End Distance` an off-screen marker is hidden,
	 * as an unclamped one is, and it goes away across the stretch before it.
	 *
	 * Zero by default, so clamping behaves as it always has until a game asks for the limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Screen Edge",
		meta = (EditCondition = "bClampToScreenEdge", EditConditionHides, ClampMin = "0.0",
			ForceUnits = "cm"))
	float ClampStartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Screen Edge",
		meta = (EditCondition = "bClampToScreenEdge", EditConditionHides, ClampMin = "0.0",
			ForceUnits = "cm"))
	float ClampEndDistance = 0.0f;

	/** What a clamped marker does across that stretch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Screen Edge",
		meta = (EditCondition = "bClampToScreenEdge && ClampEndDistance > 0.0", EditConditionHides))
	EFolioMarkerTransition ClampTransition = EFolioMarkerTransition::FadeAndScale;

	/**
	 * The size a scaling transition has shrunk to when it stops drawing, as a fraction.
	 *
	 * Not zero by default: something that shrinks the whole way to a point spends its last frames as
	 * an unreadable speck, and stopping short of that reads as a cut rather than a collapse.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditConditionHides,
			EditCondition = "DistanceTransition == EFolioMarkerTransition::Scale || DistanceTransition == EFolioMarkerTransition::FadeAndScale || ClampTransition == EFolioMarkerTransition::Scale || ClampTransition == EFolioMarkerTransition::FadeAndScale"))
	float TransitionScale = 0.35f;

	/**
	 * Which player sees it. Zero is the first local player, which is the answer in a single-player game.
	 *
	 * A marker is drawn for one viewport, because a projection is only meaningful for one camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0"))
	int32 PlayerIndex = 0;

	/** Drawn above markers with a lower value. Equal values draw in registration order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	int32 SortOrder = 0;

	/** Loads a different document, replacing whatever is up. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void LoadDocument(const FString& InDocumentPath);

	/**
	 * Binds a UObject's properties as a named data model, so the document can read them.
	 *
	 * Must be called before the document loads: RmlUi resolves a document's bindings as it loads and
	 * offers no way to attach a model afterwards. Bind in BeginPlay before setting DocumentPath, or
	 * call this and then LoadDocument.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void BindViewModel(const FString& ModelName, UObject* Source, bool bPollEveryFrame = true);

	/** Says the document has changed and must be updated and repainted. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void MarkDirty();

	/** Where this marker landed last frame, in viewport pixels. Only meaningful while it is visible. */
	UFUNCTION(BlueprintPure, Category = "Folio")
	FVector2D GetScreenPosition() const { return ScreenPosition; }

	/** Whether it drew last frame: on screen, in front of the camera, and not faded out. */
	UFUNCTION(BlueprintPure, Category = "Folio")
	bool IsOnScreen() const { return bOnScreen; }

	/** Every registered marker. The layer widget walks this; a game has no reason to. */
	static const TArray<UFolioMarkerDocument*>& GetLive();

	/** What the layer needs to paint one marker, resolved on the game thread during tick. */
	struct FPlacement
	{
		FFolioDocumentHost* Host = nullptr;
		/** Top-left of the document in viewport pixels, anchor and offset already applied. */
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		float Scale = 1.0f;
		float Opacity = 1.0f;
		int32 SortOrder = 0;
		int32 PlayerIndex = 0;
	};

	/** Null when the marker did not draw this frame. */
	const FPlacement* GetPlacement() const { return bOnScreen ? &Placement : nullptr; }

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

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Applies every requested binding to the current context. */
	void ApplyBindings();

	/** Fills Placement and returns whether the marker should draw at all. */
	bool Project(float& OutOpacity);

	// Shared rather than unique, because this header only forward declares the host: a TSharedPtr
	// captures its deleter where the type is complete, so no consumer of this header has to be.
	TSharedPtr<FFolioDocumentHost> Host;

	struct FBinding
	{
		FString ModelName;
		TWeakObjectPtr<UObject> Source;
		bool bPollEveryFrame = true;
	};

	TArray<FBinding> Bindings;

	FPlacement Placement;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	bool bOnScreen = false;

	static TArray<UFolioMarkerDocument*> LiveMarkers;
};

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "FolioMarkerDocument.generated.h"

class FFolioDocumentHost;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FString DocumentPath;

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
	 * Shrink with distance, the way something in the world would.
	 *
	 * Off by default. A marker that stays one size is easier to read and is what most HUDs want; a
	 * nameplate that should feel attached to a body wants this on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance")
	bool bScaleWithDistance = false;

	/** Distance in world units at which the document draws at its authored size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "bScaleWithDistance", ClampMin = "1.0"))
	float ReferenceDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "bScaleWithDistance", ClampMin = "0.01"))
	float MinScale = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance",
		meta = (EditCondition = "bScaleWithDistance", ClampMin = "0.01"))
	float MaxScale = 1.5f;

	/**
	 * Fades out between these distances, and stops drawing past the second.
	 *
	 * Zero for both switches it off. Fading rather than popping matters more here than it looks: a
	 * screen with thirty markers on it reads as noise, and distance is the cheapest way to thin them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance", meta = (ClampMin = "0.0"))
	float FadeStartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio|Distance", meta = (ClampMin = "0.0"))
	float FadeEndDistance = 0.0f;

	/**
	 * Keeps an off-screen marker at the edge of the screen, pointing at where it is.
	 *
	 * Off by default, because it is right for objectives and wrong for nameplates: a clamped nameplate
	 * is a label stuck to the side of the screen with no body under it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	bool bClampToScreenEdge = false;

	/** How far in dp a clamped marker sits from the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (EditCondition = "bClampToScreenEdge"))
	float ClampMargin = 48.0f;

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
	void BindViewModel(FName ModelName, UObject* Source, bool bPollEveryFrame = true);

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
		FName ModelName;
		TWeakObjectPtr<UObject> Source;
		bool bPollEveryFrame = true;
	};

	TArray<FBinding> Bindings;

	FPlacement Placement;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	bool bOnScreen = false;

	static TArray<UFolioMarkerDocument*> LiveMarkers;
};

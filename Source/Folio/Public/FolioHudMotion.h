#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "FolioSpring.h"

#include "FolioHudMotion.generated.h"

class FFolioDocumentHost;

/** What a layer's offset responds to. */
UENUM(BlueprintType)
enum class EFolioMotionSource : uint8
{
	/** How fast the view is turning. A helmet trailing a look is this. */
	CameraTurn,

	/** How fast the pawn is moving, in view space. Sway from a run or a strafe. */
	Movement,

	/** Both, added. */
	Both,
};

/**
 * One element that trails the camera, and by how much.
 *
 * Named rather than selected, because a HUD has a handful of these and each wants its own weight: a
 * visor frame barely moves, a reticle moves a little, a floating readout moves most. Three layers at
 * different weights is what reads as depth.
 */
USTRUCT(BlueprintType)
struct FOLIO_API FFolioMotionLayer
{
	GENERATED_BODY()

	/** Element id in the document, without the `#`. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FString ElementId;

	/** What it reacts to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	EFolioMotionSource Source = EFolioMotionSource::CameraTurn;

	/**
	 * Displacement in dp at the reference rate, per axis.
	 *
	 * Negative trails the motion, which is what a physical object does: turn right and a loosely
	 * mounted visor swings left before catching up. Positive leads it, which reads as the HUD being
	 * eager rather than heavy and is almost never what is wanted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D Weight = FVector2D(-18.0, -12.0);

	/** How far it may travel, in dp. Stops a spin from throwing the layer off screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0.0"))
	float MaxOffset = 40.0f;

	/** Response. Softer than the layer in front of it is what gives the stack its depth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FFolioSpring Spring;

	/** Live state, not authored. */
	FVector2f Applied = FVector2f::ZeroVector;
	bool bWasAtRest = false;
};

/**
 * Makes a HUD respond to the camera and the pawn, the way a diegetic one does.
 *
 * A visor that lags a hard turn, a reticle that drifts and settles, a readout that sways with a run.
 * None of that is expressible in RCSS: a transition fires on a state change and runs a fixed curve,
 * where this is a continuous response to a signal that never settles and is kicked again before it
 * arrives. That needs velocity carried between frames, which a stylesheet has nowhere to put.
 *
 * Put this on the actor owning the document - a UFolioWidget in a HUD, or a world document on a
 * helmet mesh - point each layer at an element id, and give the ones further back a softer spring.
 *
 * It writes `transform` and nothing else, which is deliberate: a transform triggers no reflow, so
 * driving one every frame costs a matrix rather than a layout. Animating `left` here would make a
 * HUD measurably slower than the UMG it replaced.
 */
UCLASS(ClassGroup = (Folio), meta = (BlueprintSpawnableComponent, DisplayName = "Folio HUD Motion"))
class FOLIO_API UFolioHudMotion : public UActorComponent
{
	GENERATED_BODY()

public:
	UFolioHudMotion();

	/** The elements that move, and how much each does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	TArray<FFolioMotionLayer> Layers;

	/**
	 * The turn rate that produces a layer's full Weight, in degrees per second.
	 *
	 * Everything scales against this, so one number retunes the whole HUD's sensitivity without
	 * touching a layer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "1.0"))
	float ReferenceTurnRate = 180.0f;

	/** The speed that produces a layer's full Weight, in world units per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "1.0"))
	float ReferenceSpeed = 600.0f;

	/** Which player's camera drives it. Zero is the first local player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0"))
	int32 PlayerIndex = 0;

	/**
	 * Puts every layer back at rest immediately.
	 *
	 * For a teleport or a camera cut, where springing across the whole screen is a smear rather than
	 * a flourish.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void SnapToRest();

	/**
	 * The document to drive.
	 *
	 * Found automatically from a UFolioWidget or a world document on the same actor, so the usual case
	 * needs nothing. Set it when an actor owns more than one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void SetDocumentSource(UObject* Source);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** The host of whichever component on this actor owns a document. */
	FFolioDocumentHost* FindHost() const;

	/** Turn rate in degrees per second, as yaw and pitch. */
	FVector2f ReadCameraTurn(float DeltaSeconds);

	/** Pawn velocity in view space, as right and up. */
	FVector2f ReadMovement() const;

	UPROPERTY()
	TObjectPtr<UObject> DocumentSource;

	FRotator LastViewRotation = FRotator::ZeroRotator;
	bool bHasLastRotation = false;
};

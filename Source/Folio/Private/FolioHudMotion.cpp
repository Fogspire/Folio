#include "FolioHudMotion.h"

#include "FolioDocumentHost.h"

#include "FolioMarkerDocument.h"
#include "FolioModule.h"
#include "FolioRmlUi.h"
#include "FolioWidget.h"
#include "FolioWorldDocument.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

UFolioHudMotion::UFolioHudMotion()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// After the camera has moved for the frame. Reading it before means the HUD responds to where the
	// view was last frame, which is a lag on top of the lag this is deliberately creating.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UFolioHudMotion::BeginPlay()
{
	Super::BeginPlay();
	SnapToRest();
}

void UFolioHudMotion::SetDocumentSource(UObject* Source)
{
	DocumentSource = Source;
}

void UFolioHudMotion::SnapToRest()
{
	for (FFolioMotionLayer& Layer : Layers)
	{
		Layer.Spring.Snap(FVector2f::ZeroVector);
		Layer.Applied = FVector2f::ZeroVector;
		Layer.bWasAtRest = false;
	}

	bHasLastRotation = false;
}

FFolioDocumentHost* UFolioHudMotion::FindHost() const
{
	// Whatever was named, then whatever the actor has. A HUD actor owns one document in every case
	// this is for, so searching is what makes the component work with no setup.
	if (const UFolioWorldDocument* World = Cast<UFolioWorldDocument>(DocumentSource))
	{
		return World->GetHost();
	}

	if (const AActor* Owner = GetOwner())
	{
		if (UFolioWorldDocument* World = Owner->FindComponentByClass<UFolioWorldDocument>())
		{
			return World->GetHost();
		}
	}

	return nullptr;
}

FVector2f UFolioHudMotion::ReadCameraTurn(const float DeltaSeconds)
{
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(GetWorld(), PlayerIndex);
	if (!Controller || !Controller->PlayerCameraManager || DeltaSeconds <= 0.0f)
	{
		return FVector2f::ZeroVector;
	}

	const FRotator Rotation = Controller->PlayerCameraManager->GetCameraRotation();

	if (!bHasLastRotation)
	{
		LastViewRotation = Rotation;
		bHasLastRotation = true;
		return FVector2f::ZeroVector;
	}

	// Through FRotator's own delta, so a turn across 180 degrees is a small number rather than a
	// 359-degree spike that throws the whole HUD across the screen.
	const FRotator Delta = (Rotation - LastViewRotation).GetNormalized();
	LastViewRotation = Rotation;

	return FVector2f(float(Delta.Yaw) / DeltaSeconds, float(Delta.Pitch) / DeltaSeconds);
}

FVector2f UFolioHudMotion::ReadMovement() const
{
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(GetWorld(), PlayerIndex);
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return FVector2f::ZeroVector;
	}

	const APawn* Pawn = Controller->GetPawn();
	if (!Pawn)
	{
		return FVector2f::ZeroVector;
	}

	// In view space, not world space: a HUD sways with movement across the screen, so what matters is
	// how much of the velocity is sideways and how much is vertical from where the player is looking.
	const FRotator View = Controller->PlayerCameraManager->GetCameraRotation();
	const FVector Velocity = Pawn->GetVelocity();

	return FVector2f(
		float(FVector::DotProduct(Velocity, FRotationMatrix(View).GetUnitAxis(EAxis::Y))),
		float(FVector::DotProduct(Velocity, FRotationMatrix(View).GetUnitAxis(EAxis::Z))));
}

void UFolioHudMotion::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FFolioDocumentHost* Host = FindHost();
	if (!Host || Layers.IsEmpty())
	{
		return;
	}

	Rml::ElementDocument* Document = Host->GetDocument();
	if (!Document)
	{
		return;
	}

	const FVector2f Turn = ReadCameraTurn(DeltaTime);
	const FVector2f Move = ReadMovement();

	// Normalised against the reference rates, so a layer's Weight is in dp and one pair of numbers
	// retunes the whole HUD's sensitivity.
	const FVector2f TurnSignal = Turn / FMath::Max(1.0f, ReferenceTurnRate);
	const FVector2f MoveSignal = Move / FMath::Max(1.0f, ReferenceSpeed);

	bool bAnyMoved = false;

	for (FFolioMotionLayer& Layer : Layers)
	{
		if (Layer.ElementId.IsEmpty())
		{
			continue;
		}

		FVector2f Signal = FVector2f::ZeroVector;
		switch (Layer.Source)
		{
		case EFolioMotionSource::CameraTurn: Signal = TurnSignal; break;
		case EFolioMotionSource::Movement:   Signal = MoveSignal; break;
		case EFolioMotionSource::Both:       Signal = TurnSignal + MoveSignal; break;
		}

		FVector2f Target(Signal.X * float(Layer.Weight.X), Signal.Y * float(Layer.Weight.Y));
		Target = Target.GetClampedToMaxSize(Layer.MaxOffset);

		Layer.Spring.Advance(Target, DeltaTime);

		const FVector2f Value = Layer.Spring.Value.GetClampedToMaxSize(Layer.MaxOffset);
		const bool bAtRest = Layer.Spring.IsAtRest(Target);

		// Nothing is written once it has settled and stopped moving, which is what lets the document go
		// idle: a HUD that wrote the same transform every frame would never stop invalidating, and the
		// invalidation path exists precisely so a still screen costs nothing.
		if (bAtRest && Layer.bWasAtRest)
		{
			continue;
		}

		// Rounded to a tenth of a dp before comparing, so a spring creeping through the last hundredth
		// of its travel does not keep the document awake for a difference nobody can see.
		if ((Value - Layer.Applied).IsNearlyZero(0.05f) && Layer.bWasAtRest == bAtRest)
		{
			continue;
		}

		Rml::Element* Element = Document->GetElementById(Folio::ToRmlString(Layer.ElementId));
		if (!Element)
		{
			continue;
		}

		// SetProperty rather than a data binding: data-style-* writes an inline style through the model,
		// which a stylesheet-only reload will not touch and which a second writer would fight over.
		//
		// transform rather than left/top, because a transform triggers no reflow. This runs every frame
		// on every layer, so the difference is a matrix against a layout.
		Element->SetProperty("transform",
			Folio::ToRmlString(FString::Printf(TEXT("translate(%.2fdp, %.2fdp)"), Value.X, Value.Y)));

		Layer.Applied = Value;
		Layer.bWasAtRest = bAtRest;
		bAnyMoved = true;
	}

	if (bAnyMoved)
	{
		Host->MarkDirty(TEXT("hud motion"));
	}
}

#include "FolioMarkerDocument.h"

#include "FolioDocumentHost.h"

#include "FolioMarkerLayer.h"
#include "FolioModule.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

TArray<UFolioMarkerDocument*> UFolioMarkerDocument::LiveMarkers;

const TArray<UFolioMarkerDocument*>& UFolioMarkerDocument::GetLive()
{
	return LiveMarkers;
}

UFolioMarkerDocument::UFolioMarkerDocument()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// After the camera has moved for the frame. Projecting before it means every marker trails the
	// view by a frame, which reads as the whole HUD lagging rather than as a marker being wrong.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UFolioMarkerDocument::OnRegister()
{
	Super::OnRegister();

	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		return;
	}

	// Only in a game. A marker is a projection through a player's camera, and there is no player in
	// the level editor - a preview would have to invent a view and would be showing something else.
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	Host = MakeShared<FFolioDocumentHost>();

	if (!Host->Create(TEXT("marker"), FIntPoint(
		FMath::Max(1, FMath::RoundToInt(Size.X)),
		FMath::Max(1, FMath::RoundToInt(Size.Y)))))
	{
		Host.Reset();
		return;
	}

	LiveMarkers.AddUnique(this);
	FFolioMarkerLayer::EnsureAttached(World);

	// Bound before the document loads, because a document resolves its bindings as it loads and RmlUi
	// gives no way to attach a model afterwards.
	ApplyBindings();

	if (!DocumentPath.IsEmpty())
	{
		Host->LoadDocument(DocumentPath);
	}
}

void UFolioMarkerDocument::OnUnregister()
{
	LiveMarkers.RemoveSingleSwap(this);
	Host.Reset();
	bOnScreen = false;

	Super::OnUnregister();
}

void UFolioMarkerDocument::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Host.IsValid())
	{
		return;
	}

	float Opacity = 1.0f;
	bOnScreen = IsVisible() && Project(Opacity);

	if (!bOnScreen)
	{
		// Still updated. A marker that stopped animating while off screen would arrive mid-animation
		// when it came back, and the invalidation path already makes an idle document nearly free.
		Host->Update(DeltaTime, 1.0f, 1.0f);
		return;
	}

	const FIntPoint Document(
		FMath::Max(1, FMath::RoundToInt(Size.X)),
		FMath::Max(1, FMath::RoundToInt(Size.Y)));

	Host->SetSize(Document);
	Host->Update(DeltaTime, 1.0f, 1.0f);

	Placement.Host = Host.Get();
	Placement.Size = Size;
	Placement.Opacity = Opacity;
	Placement.SortOrder = SortOrder;
	Placement.PlayerIndex = PlayerIndex;
}

bool UFolioMarkerDocument::Project(float& OutOpacity)
{
	const UWorld* World = GetWorld();
	APlayerController* Controller = UGameplayStatics::GetPlayerController(World, PlayerIndex);
	if (!Controller || !Controller->PlayerCameraManager)
	{
		return false;
	}

	const FVector Location = GetComponentLocation();
	const float Distance = float(FVector::Dist(Controller->PlayerCameraManager->GetCameraLocation(), Location));

	OutOpacity = 1.0f;

	if (FadeEndDistance > 0.0f && FadeEndDistance > FadeStartDistance)
	{
		if (Distance >= FadeEndDistance)
		{
			return false;
		}

		if (Distance > FadeStartDistance)
		{
			OutOpacity = 1.0f - (Distance - FadeStartDistance) / (FadeEndDistance - FadeStartDistance);
		}
	}

	FVector2D Screen = FVector2D::ZeroVector;
	const bool bInFront = UGameplayStatics::ProjectWorldToScreen(Controller, Location, Screen);

	// Viewport pixels in, dp out. Everything below is in dp because that is what the document is laid
	// out in, and mixing the two is the mistake this exists to stop a game making.
	const float DpiScale = UWidgetLayoutLibrary::GetViewportScale(World);
	const float SafeDpi = DpiScale > KINDA_SMALL_NUMBER ? DpiScale : 1.0f;

	FVector2D Viewport = FVector2D(1920.0, 1080.0);
	if (World->GetGameViewport())
	{
		World->GetGameViewport()->GetViewportSize(Viewport);
	}

	const FVector2D ViewportDp = Viewport / SafeDpi;
	FVector2D Anchored = Screen / SafeDpi;

	const float Scale = bScaleWithDistance
		? FMath::Clamp(ReferenceDistance / FMath::Max(1.0f, Distance), MinScale, MaxScale)
		: 1.0f;

	if (!bInFront || Anchored.X < 0.0 || Anchored.Y < 0.0
		|| Anchored.X > ViewportDp.X || Anchored.Y > ViewportDp.Y)
	{
		if (!bClampToScreenEdge)
		{
			return false;
		}

		// Behind the camera, the projection mirrors through the origin, so the direction has to be
		// flipped before it means anything. Without this a marker behind you is pinned to the opposite
		// edge from where it actually is.
		const FVector2D Centre = ViewportDp * 0.5;
		FVector2D Direction = Anchored - Centre;
		if (!bInFront)
		{
			Direction = -Direction;
		}

		if (Direction.IsNearlyZero())
		{
			Direction = FVector2D(0.0, -1.0);
		}

		Direction.Normalize();

		const FVector2D Limit = Centre - FVector2D(ClampMargin, ClampMargin);
		const double Steps = FMath::Min(
			Limit.X / FMath::Max(UE_KINDA_SMALL_NUMBER, FMath::Abs(Direction.X)),
			Limit.Y / FMath::Max(UE_KINDA_SMALL_NUMBER, FMath::Abs(Direction.Y)));

		Anchored = Centre + Direction * Steps;
	}

	Anchored += ScreenOffset;

	ScreenPosition = Anchored * SafeDpi;

	// The anchor is a fraction of the *scaled* document, so a shrinking nameplate keeps its bottom
	// centre on the same point rather than drifting off it.
	Placement.Position = Anchored - Size * Anchor * double(Scale);
	Placement.Scale = Scale;

	return true;
}

void UFolioMarkerDocument::LoadDocument(const FString& InDocumentPath)
{
	DocumentPath = InDocumentPath;

	if (Host.IsValid())
	{
		Host->LoadDocument(InDocumentPath);
	}
}

void UFolioMarkerDocument::BindViewModel(const FName ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (ModelName.IsNone() || !Source)
	{
		return;
	}

	FBinding& Binding = Bindings.AddDefaulted_GetRef();
	Binding.ModelName = ModelName;
	Binding.Source = Source;
	Binding.bPollEveryFrame = bPollEveryFrame;

	ApplyBindings();
}

void UFolioMarkerDocument::ApplyBindings()
{
	if (!Host.IsValid())
	{
		return;
	}

	for (const FBinding& Binding : Bindings)
	{
		UObject* Source = Binding.Source.Get();
		if (Source && !Host->HasObjectModel(Binding.ModelName.ToString()))
		{
			Host->BindObjectModel(Binding.ModelName.ToString(), Source, Binding.bPollEveryFrame);
		}
	}
}

void UFolioMarkerDocument::MarkDirty()
{
	if (Host.IsValid())
	{
		Host->MarkDirty(TEXT("game code"));
	}
}

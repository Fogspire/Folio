#include "FolioWorldDocument.h"

#include "FolioDocumentHost.h"
#include "FolioDocumentPath.h"
#include "FolioInput.h"
#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"
#include "FolioSettings.h"
#include "FolioWorldPass.h"
#include "SFolioWorldFocus.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "GenerateMips.h"
#include "RHIGlobals.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	TArray<UFolioWorldDocument*> LiveDocuments;

	TAutoConsoleVariable<bool> CVarShowSurface(
		TEXT("folio.World.ShowSurface"),
		false,
		TEXT("Draws every world document's rectangle in the world, and where the pointer lands on it. "
			"Green means the pointer is on the surface. In Surface mode nothing about this rectangle is "
			"drawn normally, so it can be wrong while the picture is perfect - which presents as input "
			"doing nothing."));

	TAutoConsoleVariable<bool> CVarSurfaceAutoResolution(
		TEXT("folio.World.SurfaceAutoResolution"),
		true,
		TEXT("Size a Surface document's offscreen target from how large it lands on screen, so walking up to "
			"it sharpens it. Off pins the target to the document's own size, which is the fixed-resolution "
			"behaviour a UWidgetComponent has."));

	/** Coarse enough that ordinary movement does not cross a step every frame. */
	constexpr int32 SurfaceSizeStep = 128;

	/** Shift, control and alt as RmlUi wants them, taken from the keyboard rather than from an event. */
	int32 CurrentModifiers()
	{
		return FSlateApplication::IsInitialized()
			? Folio::ToRmlModifiers(FSlateApplication::Get().GetModifierKeys())
			: 0;
	}

	/**
	 * Shrinks a size until it fits what the RHI can allocate, keeping its aspect ratio.
	 *
	 * A render target past the device limit asserts inside the RHI rather than failing to create, so an
	 * unreasonable PixelsPerUnit has to be caught here. Proportional rather than per-axis: clamping one
	 * edge would silently stretch the document instead.
	 */
	FIntPoint FitToTextureLimit(const FIntPoint Size)
	{
		const int32 Limit = FMath::Max(1, int32(GMaxTextureDimensions));
		const int32 Longest = FMath::Max(Size.X, Size.Y);
		if (Longest <= Limit)
		{
			return Size;
		}

		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogFolio, Warning,
				TEXT("a world document asks for %dx%d, past the %d texture limit, so it is being scaled down "
					"to fit. Lower PixelsPerUnit or WorldSize."), Size.X, Size.Y, Limit);
		}

		const double Scale = double(Limit) / double(Longest);
		return FIntPoint(
			FMath::Max(1, FMath::FloorToInt(double(Size.X) * Scale)),
			FMath::Max(1, FMath::FloorToInt(double(Size.Y) * Scale)));
	}
}

const TArray<UFolioWorldDocument*>& UFolioWorldDocument::GetLive()
{
	return LiveDocuments;
}

UFolioWorldDocument::UFolioWorldDocument()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Authoring a surface means seeing it in the level viewport, and the recording only happens on tick, so
	// without this a placed document draws nothing until play begins. Follows bUpdateInEditor from here on.
	bTickInEditor = true;

	// The document updates itself, which has nothing to do with whether the surface is on screen. A
	// document that stopped updating while looked away from would arrive stale, and the invalidation
	// path already makes an idle one nearly free.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// The project's default, so a game that has made this decision does not repeat it on every component.
	// Read here rather than at registration because a value set on the component has to win, and by then
	// the two are indistinguishable.
	DrawMode = GetDefault<UFolioSettings>()->DefaultWorldDrawMode;

	PointerKey = EKeys::LeftMouseButton;
}

FIntPoint UFolioWorldDocument::GetDocumentSize() const
{
	return FitToTextureLimit(FIntPoint(
		FMath::Max(1, FMath::RoundToInt(WorldSize.X * PixelsPerUnit)),
		FMath::Max(1, FMath::RoundToInt(WorldSize.Y * PixelsPerUnit))));
}

void UFolioWorldDocument::GetSurface(FVector& OutCentre, FVector& OutRight, FVector& OutUp) const
{
	const FTransform& Transform = GetComponentTransform();

	OutCentre = Transform.GetLocation();

	// The surface lies in the component's local XY, facing along local Z, so it faces the way an actor
	// arrow does and can be aimed by pointing the component at the viewer.
	//
	// Half extents rather than full: the document's own space runs from a corner, and a renderer needs
	// the centre and the two axes to get from one to the other.
	OutRight = Transform.GetUnitAxis(EAxis::X) * (WorldSize.X * 0.5) * Transform.GetScale3D().X;
	OutUp = Transform.GetUnitAxis(EAxis::Y) * (WorldSize.Y * 0.5) * Transform.GetScale3D().Y;
}

void UFolioWorldDocument::OnRegister()
{
	Super::OnRegister();

	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		return;
	}

	// A component is registered in the editor as well as in play, and a document that renders in the
	// level viewport is the point of authoring one, so this is not gated on a game world.
	bTickInEditor = bUpdateInEditor;

	Host = MakeShared<FFolioDocumentHost>();
	Host->SetReferencer(this);

	// The cursor belongs to the document, so a reload or a hot reload takes it with it.
	Host->OnDocumentClosing = [this] { Cursor = nullptr; CursorAt.Reset(); };

	if (!Host->Create(TEXT("world"), GetDocumentSize()))
	{
		Host.Reset();
		return;
	}

	LiveDocuments.AddUnique(this);

	// Bound before the document loads, because a document resolves its bindings as it loads and RmlUi
	// gives no way to attach a model afterwards.
	ApplyBindings();

	if (!DocumentPath.IsEmpty())
	{
		Host->LoadDocument(DocumentPath);
	}
}

void UFolioWorldDocument::OnUnregister()
{
	LiveDocuments.RemoveSingleSwap(this);

	// Before the host goes: the sink holds a raw pointer to it, and it is what gives the keyboard back.
	if (bHoldingFocus)
	{
		SFolioWorldFocus::Detach(Host.Get());
		bHoldingFocus = false;
	}

	Host.Reset();
	ReleaseRecordings();
	SurfaceTarget.Reset();
	SurfaceSize = FIntPoint::ZeroValue;

	Super::OnUnregister();
}

void UFolioWorldDocument::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Host.IsValid())
	{
		return;
	}

	// Anything no frame is reading any more goes here, on the game thread. See Retired.
	Retired.RemoveAll([](const TSharedPtr<const FFolioDrawList>& List) { return List.IsUnique(); });

	Host->TickDataModels();

	// Before the update, so a click and what it changed land in the same frame.
	if (bPointerFromPlayerView)
	{
		TickPlayerPointer();
	}
	UpdateKeyboardFocus();
	UpdateCursor();
	DrawDebugSurface();

	const bool bSurface = DrawMode == EFolioWorldDrawMode::Surface;
	const FIntPoint Document = GetDocumentSize();
	const FIntPoint TargetSize = bSurface ? DesiredSurfaceSize() : Document;

	// Laid out in its own space at every resolution, and the replay scales. Laying out against the target
	// instead would re-round every length each time the surface changed resolution, and walking towards a
	// screen would visibly reflow it. Nothing is lost: the fields carry their own edges, so the same
	// geometry drawn into a larger target is sharper rather than bigger.
	Host->SetSize(Document);

	if (bSurface && TargetSize != SurfaceSize)
	{
		// The new target starts empty, and a settled document would otherwise decline to redraw it.
		Host->MarkDirty(TEXT("surface resolution"));
	}

	// One document pixel expressed in target pixels, which only the background antialiasing needs: the
	// distance fields resolve their own edges and need no telling.
	const float RenderScale = float(Document.Y) / float(FMath::Max(1, TargetSize.Y));

	if (!Host->Update(DeltaTime, 1.0f, RenderScale))
	{
		// Nothing changed, so the recording from last time still describes the document. Re-recording it
		// would be the same work the invalidation path exists to avoid.
		return;
	}

	Rml::Context* Context = Host->GetContext();
	FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface();
	if (!Context || !Renderer)
	{
		return;
	}

	if (bSurface)
	{
		RenderToSurface(TargetSize);
		return;
	}

	// A new list rather than a rewrite of the published one: a render pass reads that list for as long as
	// its frame is in flight, which outlasts this tick. Rewriting it under the pass is a data race, and it
	// presents as geometry flickering while the camera moves and eventually as a freed texture.
	const TSharedRef<FFolioDrawList> Next = MakeShared<FFolioDrawList>();

	// Which space the destination is in, which the two routes disagree about. Scene colour after the
	// tonemapper is already gamma encoded, so a byte colour goes through as written. Scene colour before
	// post-processing is linear HDR, so the same byte colour has to be decoded first.
	Next->SetDecodeSrgb(DrawMode == EFolioWorldDrawMode::InScene);

	// The same Render call the screen path makes, with the recording installed instead of Slate. RmlUi
	// does not know the difference, which is the point.
	Renderer->BeginRecording(*Next, GetDocumentSize());
	Context->Render();
	Renderer->EndFrame();

	if (Published.IsValid())
	{
		Retired.Add(Published);
	}
	Published = Next;
}

void UFolioWorldDocument::ReleaseRecordings()
{
	const auto StillInFlight = [](const TSharedPtr<const FFolioDrawList>& List)
	{
		return List.IsValid() && !List.IsUnique();
	};

	if (StillInFlight(Published) || Retired.ContainsByPredicate(StillInFlight))
	{
		// The pass's snapshot holds them too, and is not replaced once no document is left to draw.
		if (FFolioWorldPass* Pass = FFolioModule::IsAvailable() ? FFolioModule::Get().GetWorldPass() : nullptr)
		{
			TArray<const FFolioDrawList*> Lists;
			Lists.Add(Published.Get());
			for (const TSharedPtr<const FFolioDrawList>& List : Retired)
			{
				Lists.Add(List.Get());
			}
			Pass->ForgetDrawLists(MoveTemp(Lists));
		}
		FlushRenderingCommands();
	}

	Published.Reset();
	Retired.Empty();
}

void UFolioWorldDocument::MovePointer(const FVector2D SurfaceUV)
{
	Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr;
	if (!Context)
	{
		return;
	}

	const FIntPoint Size = Host->GetSize();
	const FIntPoint Position(
		FMath::RoundToInt(FMath::Clamp(SurfaceUV.X, 0.0, 1.0) * double(Size.X)),
		FMath::RoundToInt(FMath::Clamp(SurfaceUV.Y, 0.0, 1.0) * double(Size.Y)));

	// Compared in the coordinates RmlUi receives rather than against the ray, so a head that moves
	// without the aim leaving a glyph does not count as a move and the document can still be idle.
	if (Pointer.IsSet() && Pointer.GetValue() == Position)
	{
		return;
	}

	Pointer = Position;
	Host->MarkDirty(TEXT("pointer move"));
	Context->ProcessMouseMove(Position.X, Position.Y, CurrentModifiers());
}

bool UFolioWorldDocument::MovePointerAlong(const FVector Origin, const FVector Direction,
	const float Distance)
{
	FVector Centre;
	FVector Right;
	FVector Up;
	GetSurface(Centre, Right, Up);

	const FVector Normal = FVector::CrossProduct(Right, Up).GetSafeNormal();
	const FVector Ray = Direction.GetSafeNormal();

	const double Slope = FVector::DotProduct(Normal, Ray);
	if (FMath::IsNearlyZero(Slope))
	{
		LastMiss = EPointerMiss::Parallel;
		return false;
	}

	const double Along = FVector::DotProduct(Centre - Origin, Normal) / Slope;
	if (Along < 0.0)
	{
		LastMiss = EPointerMiss::Behind;
		return false;
	}

	if (Along > double(Distance))
	{
		LastMiss = EPointerMiss::TooFar;
		return false;
	}

	// Right and Up are half extents, so these land in -1 to 1 across the surface.
	const FVector Local = (Origin + Ray * Along) - Centre;
	const double U = FVector::DotProduct(Local, Right) / Right.SizeSquared();
	const double V = FVector::DotProduct(Local, Up) / Up.SizeSquared();
	if (FMath::Abs(U) > 1.0 || FMath::Abs(V) > 1.0)
	{
		LastMiss = EPointerMiss::Outside;
		return false;
	}

	LastMiss = EPointerMiss::Landed;

	// Document space runs down from the top left, and the surface's up axis runs the other way.
	MovePointer(ApplyPointerFlips(FVector2D(U * 0.5 + 0.5, 0.5 - V * 0.5)));
	return true;
}

FVector2D UFolioWorldDocument::ApplyPointerFlips(const FVector2D UV) const
{
	return FVector2D(bInvertPointerU ? 1.0 - UV.X : UV.X, bInvertPointerV ? 1.0 - UV.Y : UV.Y);
}

bool UFolioWorldDocument::MovePointerToHit(const FHitResult& Hit)
{
	FVector2D UV = FVector2D::ZeroVector;
	if (!UGameplayStatics::FindCollisionUV(Hit, FMath::Max(0, PointerUVChannel), UV))
	{
		LastMiss = EPointerMiss::NoMeshUv;
		return false;
	}

	LastMiss = EPointerMiss::Landed;
	MovePointer(ApplyPointerFlips(UV));
	return true;
}

void UFolioWorldDocument::ClearPointer()
{
	// A held button means a drag is in progress, and leaving tells RmlUi the mouse is gone: it stops
	// tracking what is under the pointer, so the release lands on nothing and the drop never happens.
	// Aim wanders off a screen constantly, so this is the difference between a drop that works and one
	// that works only when the player is careful.
	if (HeldButtons != 0)
	{
		return;
	}

	if (!Pointer.IsSet())
	{
		return;
	}

	Pointer.Reset();

	if (Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr)
	{
		// Without this the last hovered element keeps :hover indefinitely.
		Host->MarkDirty(TEXT("pointer leave"));
		Context->ProcessMouseLeave();
	}
}

void UFolioWorldDocument::PressPointer(const FKey Button)
{
	Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr;
	const int32 Index = Folio::ToRmlMouseButton(Button);
	if (!Context || Index < 0 || !Pointer.IsSet())
	{
		return;
	}

	if (CVarShowSurface.GetValueOnGameThread())
	{
		// What was clicked matters more than where the pointer was: this is the line to compare against
		// what the document actually did.
		UE_LOG(LogFolio, Display, TEXT("world press on '%s' at %d,%d of %dx%d, on %s"), *DocumentPath,
			Pointer.GetValue().X, Pointer.GetValue().Y, Host->GetSize().X, Host->GetSize().Y,
			*DescribeHover());
	}

	HeldButtons |= 1 << Index;

	Host->MarkDirty(TEXT("pointer down"));
	Context->ProcessMouseButtonDown(Index, CurrentModifiers());
}

void UFolioWorldDocument::ReleasePointer(const FKey Button)
{
	Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr;
	const int32 Index = Folio::ToRmlMouseButton(Button);
	if (!Context || Index < 0)
	{
		return;
	}

	HeldButtons &= ~(1 << Index);

	if (CVarShowSurface.GetValueOnGameThread())
	{
		UE_LOG(LogFolio, Display, TEXT("world release on '%s', over %s"), *DocumentPath, *DescribeHover());
	}

	// Sent even with the pointer off the surface, so a press that ends elsewhere still completes rather
	// than leaving the document believing a button is held.
	Host->MarkDirty(TEXT("pointer up"));
	Context->ProcessMouseButtonUp(Index, CurrentModifiers());
}

void UFolioWorldDocument::ScrollPointer(const float Lines)
{
	if (Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr)
	{
		Host->MarkDirty(TEXT("pointer wheel"));
		Context->ProcessMouseWheel(Lines, CurrentModifiers());
	}
}

bool UFolioWorldDocument::IsTakingText() const
{
	return Host.IsValid() && Host->IsTakingText();
}

void UFolioWorldDocument::FocusDocument()
{
	bFocusRequested = true;

	// The document itself, so the first direction moves to something rather than nowhere. RmlUi
	// navigates from whatever holds focus, and a document with none swallows the first press.
	if (Host.IsValid())
	{
		if (Rml::ElementDocument* Document = Host->GetDocument())
		{
			Document->Focus();
		}
	}

	UpdateKeyboardFocus();
}

void UFolioWorldDocument::ReleaseFocus()
{
	bFocusRequested = false;

	Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr;
	if (!Context)
	{
		return;
	}

	if (Rml::Element* Focused = Context->GetFocusElement())
	{
		Host->MarkDirty(TEXT("focus released"));
		Focused->Blur();
	}

	UpdateKeyboardFocus();
}

void UFolioWorldDocument::UpdateCursor()
{
	Rml::ElementDocument* Document = Host.IsValid() ? Host->GetDocument() : nullptr;
	if (!Document)
	{
		Cursor = nullptr;
		return;
	}

	if (!bShowCursor)
	{
		if (Cursor)
		{
			Cursor->SetProperty("display", "none");
		}
		return;
	}

	if (!Cursor)
	{
		Rml::ElementPtr Made = Document->CreateElement("div");
		if (!Made)
		{
			return;
		}

		// Sized in dp so it stays the same size on the surface whatever resolution it renders at, and
		// taken out of hit testing so the thing it points at is what gets clicked rather than the cursor.
		Made->SetProperty("position", "absolute");
		Made->SetProperty("pointer-events", "none");
		Made->SetProperty("z-index", "2147483647");
		Made->SetProperty("width", "22dp");
		Made->SetProperty("height", "22dp");
		Made->SetProperty("margin-left", "-11dp");
		Made->SetProperty("margin-top", "-11dp");
		Made->SetProperty("border-width", "2dp");
		Made->SetProperty("border-color", "#ffffffcc");
		Made->SetProperty("box-shadow", "0 0 3dp 1dp #000000a0");

		Cursor = Document->AppendChild(std::move(Made));
	}

	// Only on a change. A property set every frame dirties the document every frame, which is exactly what
	// the invalidation path exists to avoid.
	if (!Cursor || CursorAt == Pointer)
	{
		return;
	}

	CursorAt = Pointer;

	if (!Pointer.IsSet())
	{
		Cursor->SetProperty("display", "none");
		return;
	}

	Cursor->SetProperty("display", "block");
	Cursor->SetProperty("left", Rml::String(TCHAR_TO_UTF8(*FString::Printf(TEXT("%dpx"),
		Pointer.GetValue().X))));
	Cursor->SetProperty("top", Rml::String(TCHAR_TO_UTF8(*FString::Printf(TEXT("%dpx"),
		Pointer.GetValue().Y))));
}

FString UFolioWorldDocument::DescribeHover() const
{
	Rml::Context* Context = Host.IsValid() ? Host->GetContext() : nullptr;
	Rml::Element* Hover = Context ? Context->GetHoverElement() : nullptr;
	if (!Hover)
	{
		return TEXT("nothing");
	}

	const FString Tag = UTF8_TO_TCHAR(Hover->GetTagName().c_str());
	const FString Id = UTF8_TO_TCHAR(Hover->GetId().c_str());
	return Id.IsEmpty() ? Tag : FString::Printf(TEXT("%s#%s"), *Tag, *Id);
}

void UFolioWorldDocument::DrawDebugSurface() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!CVarShowSurface.GetValueOnGameThread() || !World || !Host.IsValid())
	{
		return;
	}

	FVector Centre;
	FVector Right;
	FVector Up;
	GetSurface(Centre, Right, Up);

	const TCHAR* Why = TEXT("");
	switch (LastMiss)
	{
	case EPointerMiss::Landed:    Why = TEXT("pointing at it"); break;
	case EPointerMiss::NotDriven: Why = TEXT("nothing is driving the pointer: tick Pointer From Player "
		"View, or call Move Pointer Along"); break;
	case EPointerMiss::Parallel:  Why = TEXT("the view is edge on to the surface"); break;
	case EPointerMiss::Behind:    Why = TEXT("the surface is behind the ray: it faces the other way, so "
		"turn the component 180 degrees"); break;
	case EPointerMiss::TooFar:    Why = TEXT("further away than Pointer Reach"); break;
	case EPointerMiss::Outside:   Why = TEXT("aimed off the surface"); break;
	case EPointerMiss::NoMesh:    Why = TEXT("no owning actor to trace against"); break;
	case EPointerMiss::NoMeshUv:  Why = TEXT("the mesh hit gave no UV: switch on Support UV From Hit "
		"Results in Project Settings > Physics, and give the mesh complex collision"); break;
	}

	// Logged on change rather than every frame: the interesting thing is the transition, and a line per
	// frame buries it.
	if (LastMiss != LoggedMiss)
	{
		LoggedMiss = LastMiss;
		UE_LOG(LogFolio, Display, TEXT("world pointer on '%s': %s"), *DocumentPath, Why);
	}

	if (Pointer.IsSet())
	{
		const double Now = World->GetTimeSeconds();
		if (Now - LoggedPointerAt > 0.25)
		{
			LoggedPointerAt = Now;

			const FIntPoint Size = Host->GetSize();
			UE_LOG(LogFolio, Display, TEXT("world pointer on '%s': %d,%d of %dx%d, over %s"),
				*DocumentPath, Pointer.GetValue().X, Pointer.GetValue().Y, Size.X, Size.Y,
				*DescribeHover());
		}
	}

	const FColor Colour = Pointer.IsSet() ? FColor::Green : FColor::Red;
	const FVector Corners[4] = {
		Centre - Right + Up, Centre + Right + Up, Centre + Right - Up, Centre - Right - Up };

	for (int32 Index = 0; Index < 4; ++Index)
	{
		DrawDebugLine(World, Corners[Index], Corners[(Index + 1) % 4], Colour, false, -1.0f, 0, 0.5f);
	}

	// A corner marker, because a rectangle alone does not say which way up the document is and an
	// upside-down surface points as convincingly as a right way up one.
	DrawDebugLine(World, Corners[0], Corners[0] + (Corners[1] - Corners[0]) * 0.1f
		+ (Corners[3] - Corners[0]) * 0.1f, Colour, false, -1.0f, 0, 1.5f);

	if (!Pointer.IsSet())
	{
		return;
	}

	const FIntPoint Size = Host->GetSize();
	const double U = double(Pointer.GetValue().X) / double(FMath::Max(1, Size.X));
	const double V = double(Pointer.GetValue().Y) / double(FMath::Max(1, Size.Y));

	DrawDebugSphere(World, Centre + Right * (U * 2.0 - 1.0) + Up * (1.0 - V * 2.0),
		2.0f, 8, FColor::Yellow, false, -1.0f, 0, 0.5f);
#endif
}

bool UFolioWorldDocument::TracePointerToMesh(const FVector Origin, const FVector Direction)
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		LastMiss = EPointerMiss::NoMesh;
		return false;
	}

	// Complex, because the UV lookup reads the triangle that was hit and a simple shape has none.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FolioWorldPointer), /*bTraceComplex*/ true);
	Params.bReturnFaceIndex = true;

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction.GetSafeNormal() * PointerReach,
		PointerTraceChannel, Params))
	{
		LastMiss = EPointerMiss::Outside;
		return false;
	}

	// Something else in front of the screen is the player not pointing at it, not a failure.
	if (Hit.GetActor() != Owner)
	{
		LastMiss = EPointerMiss::Outside;
		return false;
	}

	return MovePointerToHit(Hit);
}

void UFolioWorldDocument::TickPlayerPointer()
{
	const UWorld* World = GetWorld();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!Player)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	Player->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const bool bLanded = bPointerUsesMeshUV
		? TracePointerToMesh(ViewLocation, ViewRotation.Vector())
		: MovePointerAlong(ViewLocation, ViewRotation.Vector(), PointerReach);

	if (!bLanded)
	{
		ClearPointer();
	}

	// A mouse button is asked of Slate rather than of the player controller, because under an input mode
	// of UI Only the controller never sees one and the screen would stop taking clicks.
	const bool bDown = PointerKey.IsValid()
		&& (PointerKey.IsMouseButton() && FSlateApplication::IsInitialized()
			? FSlateApplication::Get().GetPressedMouseButtons().Contains(PointerKey)
			: Player->IsInputKeyDown(PointerKey));
	if (bDown == bPointerKeyHeld)
	{
		return;
	}

	bPointerKeyHeld = bDown;
	if (bDown)
	{
		PressPointer(PointerKey);
	}
	else
	{
		ReleasePointer(PointerKey);
	}
}

bool UFolioWorldDocument::FocusElement(const FString& ElementId, const bool bShowRing)
{
	Rml::ElementDocument* Document = Host.IsValid() ? Host->GetDocument() : nullptr;
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(Folio::ToRmlString(ElementId));
	return Element && Element->Focus(bShowRing);
}

FString UFolioWorldDocument::GetElementValue(const FString& ElementId) const
{
	Rml::ElementDocument* Document = Host.IsValid() ? Host->GetDocument() : nullptr;
	Rml::Element* Element = Document ? Document->GetElementById(Folio::ToRmlString(ElementId)) : nullptr;
	if (const Rml::ElementFormControl* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
	{
		return Folio::ToFString(Control->GetValue());
	}
	return FString();
}

void UFolioWorldDocument::UpdateKeyboardFocus()
{
	const UWorld* World = GetWorld();
	const bool bLive = Host.IsValid() && World && World->IsGameWorld();

	// Two ways in. Focused outright, the document owns directional input; pulled in by a text field, it
	// owns only what a field needs and the player can still walk away mid-word.
	const bool bWanted = bLive && (bFocusRequested || Host->IsTakingText());
	if (bWanted == bHoldingFocus && (!bWanted || bFocusRequested == bLastNavigating))
	{
		return;
	}

	bHoldingFocus = bWanted;
	bLastNavigating = bFocusRequested;

	if (bWanted)
	{
		SFolioWorldFocus::Attach(Host.Get(), bFocusRequested, FSimpleDelegate::CreateWeakLambda(this, [this] { OnBack.Broadcast(); }));
	}
	else
	{
		SFolioWorldFocus::Detach(Host.Get());
	}
}

int32 UFolioWorldDocument::ComputeScreenHeightPixels(const FVector& CameraLocation,
	const float FovDegrees, const int32 ViewportHeight) const
{
	if (ViewportHeight <= 0)
	{
		return 0;
	}

	const float Distance = float(FVector::Dist(GetComponentLocation(), CameraLocation));
	const float HalfFovRadians = FMath::DegreesToRadians(FMath::Clamp(FovDegrees, 1.0f, 170.0f) * 0.5f);
	const float HalfHeightAtDistance = FMath::Max(1.0f, Distance * FMath::Tan(HalfFovRadians));

	// The vertical axis because that is what FOV describes once the aspect ratio is applied, and the
	// surface is square-ish enough that one axis decides both.
	const float WorldHeight = float(WorldSize.Y * GetComponentScale().Y);

	return FMath::RoundToInt((WorldHeight * 0.5f / HalfHeightAtDistance) * float(ViewportHeight));
}

int32 UFolioWorldDocument::GetScreenHeightPixels() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Camera = Player ? Player->PlayerCameraManager : nullptr;

	if (!Camera)
	{
		return 0;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	Player->GetViewportSize(ViewportX, ViewportY);

	return ComputeScreenHeightPixels(Camera->GetCameraLocation(), Camera->GetFOVAngle(), ViewportY);
}

bool UFolioWorldDocument::SetPixelsPerUnitForScreenHeight(const int32 ScreenHeightPixels)
{
	const double WorldHeight = WorldSize.Y * GetComponentScale().Y;

	if (ScreenHeightPixels <= 0 || WorldHeight <= 0.0)
	{
		return false;
	}

	// Against the unscaled WorldSize, since that is what PixelsPerUnit multiplies. A component scaled up
	// covers more screen for the same WorldSize, and the density has to account for it.
	PixelsPerUnit = FMath::Clamp(float(double(ScreenHeightPixels) / WorldHeight)
		* float(GetComponentScale().Y), 0.1f, 200.0f);

	return true;
}

FIntPoint UFolioWorldDocument::DesiredSurfaceSize() const
{
	const FIntPoint Document = GetDocumentSize();
	const int32 MaxEdge = FMath::Max(1, FMath::RoundToInt(float(FMath::Max(Document.X, Document.Y))
		* FMath::Max(0.25f, MaxSurfaceScale)));

	const UWorld* World = GetWorld();
	if (!CVarSurfaceAutoResolution.GetValueOnGameThread() || !World || !World->IsGameWorld())
	{
		return Document;
	}

	const float ScreenPixels = float(GetScreenHeightPixels());
	if (ScreenPixels <= 0.0f)
	{
		return Document;
	}

	// Sized from the screen rather than from the document, and rounded up to a step so approaching the
	// surface raises the resolution before it looks soft rather than after.
	//
	// The floor used to be a fraction of the document's own size, which had it backwards: a document
	// laid out at 1200 that lands 14 pixels tall was given a 256-tall target and minified eighteen
	// times. Distance should cost resolution, not keep it.
	const float Ratio = FMath::Min(ScreenPixels / float(FMath::Max(1, Document.Y)), MaxSurfaceScale);
	int32 Height = FMath::Clamp(
		FMath::DivideAndRoundUp(FMath::RoundToInt(float(Document.Y) * Ratio), SurfaceSizeStep) * SurfaceSizeStep,
		SurfaceSizeStep, MaxEdge);

	// Growing happens at once, because that is the mode's whole promise. Shrinking waits until a step has
	// been given up completely: standing near a boundary otherwise flips the resolution every frame, and
	// each flip costs a reallocation.
	if (SurfaceSize.Y > 0 && Height < SurfaceSize.Y && SurfaceSize.Y - Height <= SurfaceSizeStep)
	{
		Height = SurfaceSize.Y;
	}

	// The document's aspect ratio is what decides the width: a target that did not match it would stretch.
	const int32 Width = FMath::Max(SurfaceSizeStep,
		FMath::RoundToInt(float(Height) * float(Document.X) / float(FMath::Max(1, Document.Y))));

	return FitToTextureLimit(FIntPoint(Width, Height));
}

void UFolioWorldDocument::RenderToSurface(const FIntPoint Size)
{
	Rml::Context* Context = Host->GetContext();
	FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface();
	if (!Context || !Renderer || Size.X <= 0 || Size.Y <= 0)
	{
		return;
	}

	if (!SurfaceTarget.IsValid())
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8;

		// Premultiplied transparent, matching the layer path: a surface that starts opaque black fringes
		// every edge the material later blends.
		Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

		// Allocates the mip chain. Nothing fills it automatically on this path - the document is drawn
		// through RDG rather than through a canvas - so RenderToSurface generates them by hand. Without a
		// chain a surface seen from any distance is minified with a single bilinear tap, and thin strokes
		// and text break up and crawl.
		Target->bAutoGenerateMips = true;
		Target->MipsSamplerFilter = TF_Trilinear;
		Target->MipsAddressU = TA_Clamp;
		Target->MipsAddressV = TA_Clamp;

		Target->bForceLinearGamma = false;
		Target->InitAutoFormat(Size.X, Size.Y);
		Target->UpdateResourceImmediate(true);

		SurfaceTarget.Reset(Target);
		SurfaceSize = Size;
		OnSurfaceChanged.Broadcast(Target);
	}
	else if (SurfaceSize != Size)
	{
		// Resized rather than replaced. A new object would mean every material sampling it has to rebind,
		// and until it does the mesh shows the target it was given last - so changing resolution while
		// anything is animating drops a frame or two of it. The resource is reallocated either way; what
		// this keeps is the binding.
		SurfaceTarget->ResizeTarget(Size.X, Size.Y);
		SurfaceSize = Size;

		// Still announced, for anything that tracks the size rather than the pointer. Same object.
		OnSurfaceChanged.Broadcast(SurfaceTarget.Get());
	}

	if (!Renderer->BeginSurface(GetDocumentSize(), SurfaceTarget.Get()))
	{
		return;
	}

	Context->Render();
	Renderer->EndSurface();

	GenerateSurfaceMips();
}

void UFolioWorldDocument::GenerateSurfaceMips()
{
	FTextureRenderTargetResource* Resource = SurfaceTarget.IsValid()
		? SurfaceTarget->GameThread_GetRenderTargetResource()
		: nullptr;

	if (!Resource || SurfaceTarget->GetNumMips() <= 1)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(FolioSurfaceMips)(
		[Resource](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef Texture = RegisterExternalTexture(GraphBuilder,
				Resource->GetRenderTargetTexture(), TEXT("FolioSurface"));

			FGenerateMips::Execute(GraphBuilder, GMaxRHIFeatureLevel, Texture);

			GraphBuilder.Execute();
		});
}

void UFolioWorldDocument::LoadDocument(const FString& InDocumentPath)
{
	DocumentPath = InDocumentPath;

	if (Host.IsValid())
	{
		Host->LoadDocument(InDocumentPath);
	}
}

bool UFolioWorldDocument::BindViewModel(const FString& ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (ModelName.IsEmpty() || !Source)
	{
		return false;
	}

	// Recorded rather than only applied, so a re-register - which moving the component in the editor
	// causes - puts the same models back.
	const int32 Existing = Bindings.IndexOfByPredicate(
		[&ModelName](const FPendingBinding& Binding) { return Binding.ModelName.Equals(ModelName, ESearchCase::CaseSensitive); });

	FPendingBinding Binding;
	Binding.ModelName = ModelName;
	Binding.Source = Source;
	Binding.bPollEveryFrame = bPollEveryFrame;

	if (Existing != INDEX_NONE)
	{
		Bindings[Existing] = Binding;
	}
	else
	{
		Bindings.Add(Binding);
	}

	// Strong, so a view model referenced nowhere else is not collected out from under the document.
	BoundSources.AddUnique(Source);

	if (!Host.IsValid())
	{
		return true;
	}

	const bool bDocumentAlreadyUp = Host->GetDocument() != nullptr;
	ApplyBindings();

	if (bDocumentAlreadyUp)
	{
		// The document resolved its bindings without this model and RmlUi cannot attach one afterwards,
		// so rebuilding is what makes it visible.
		UE_LOG(LogFolio, Log,
			TEXT("BindViewModel('%s') arrived after '%s' had loaded, so the document is being rebuilt. ")
			TEXT("Bind before the document loads to avoid it."),
			*ModelName, *DocumentPath);

		Host->ReloadDocument();
	}

	return true;
}

void UFolioWorldDocument::ApplyBindings()
{
	if (!Host.IsValid())
	{
		return;
	}

	for (const FPendingBinding& Binding : Bindings)
	{
		UObject* Source = Binding.Source.Get();
		if (Source && !Host->HasObjectModel(Binding.ModelName))
		{
			Host->BindObjectModel(Binding.ModelName, Source, Binding.bPollEveryFrame);
		}
	}
}

void UFolioWorldDocument::MarkDirty()
{
	if (Host.IsValid())
	{
		Host->MarkDirty(TEXT("game code"));
	}
}

void UFolioWorldDocument::ReloadDocument()
{
	if (Host.IsValid())
	{
		Host->ReloadDocument();
	}
}

void UFolioWorldDocument::ReloadStyleSheets()
{
	if (Host.IsValid())
	{
		Host->ReloadStyleSheets();
	}
}

#if WITH_EDITOR
void UFolioWorldDocument::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Property = PropertyChangedEvent.GetPropertyName();

	// Before the early-out, so a path is made portable whether or not there is a host to load it. An
	// absolute path is wrong the moment anyone else opens the level.
	if (Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, DocumentPath))
	{
		DocumentPath = UFolioDocumentPathLibrary::MakeDocumentPathPortable(DocumentPath);

		if (!DocumentPath.IsEmpty() && !UFolioDocumentPathLibrary::DocumentExists(DocumentPath))
		{
			UFolioDocumentPathLibrary::ReportMissingDocument(DocumentPath, TEXT("Folio World Document"), this);
		}
	}

	if (!Host.IsValid())
	{
		return;
	}

	if (Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, DocumentPath))
	{
		Host->LoadDocument(DocumentPath);
	}
	else if (Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, bUpdateInEditor))
	{
		// Read when the tick function is registered, so toggling it has no effect until it is re-registered.
		bTickInEditor = bUpdateInEditor;
		PrimaryComponentTick.UnRegisterTickFunction();
		PrimaryComponentTick.RegisterTickFunction(GetComponentLevel());

		const UWorld* World = GetWorld();
		if (!bUpdateInEditor && World && !World->IsGameWorld())
		{
			// Otherwise the last recording stays on screen and reads as the document still being live.
			ReleaseRecordings();
		}
	}
	else if (Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, WorldSize)
		|| Property == GET_MEMBER_NAME_CHECKED(UFolioWorldDocument, PixelsPerUnit))
	{
		// Resizing relays out the document rather than scaling it, which is the point of expressing the
		// surface in world units.
		Host->SetSize(GetDocumentSize());
	}
}
#endif

#if WITH_EDITOR

void UFolioWorldDocument::CheckForErrors()
{
	Super::CheckForErrors();
	UFolioDocumentPathLibrary::ValidateDocumentPath(*this, DocumentPath.Path, nullptr);
}

EDataValidationResult UFolioWorldDocument::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// A warning does not make the asset invalid: an unstaged document is correct in the editor and in
	// a standalone run, and failing validation over it would fail a cook that is otherwise fine.
	const EFolioDocumentPathIssue Issue =
		UFolioDocumentPathLibrary::ValidateDocumentPath(*this, DocumentPath.Path, &Context);

	if (Issue != EFolioDocumentPathIssue::None && Issue != EFolioDocumentPathIssue::Unstaged)
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#endif

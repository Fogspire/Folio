#include "FolioWorldDocument.h"

#include "FolioDocumentHost.h"
#include "FolioInput.h"
#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioSettings.h"
#include "SFolioWorldFocus.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "RHIGlobals.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>

namespace
{
	TArray<UFolioWorldDocument*> LiveDocuments;

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

	Host = MakeUnique<FFolioDocumentHost>();
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

	const bool bSurface = DrawMode == EFolioWorldDrawMode::Surface;
	const FIntPoint Document = GetDocumentSize();
	const FIntPoint TargetSize = bSurface ? DesiredSurfaceSize() : Document;

	// The context is the size of what is being drawn into, always. A surface renders at whatever
	// resolution it currently deserves, so the document is laid out there rather than in its own space
	// and scaled: geometry and glyphs are size-independent, so laying out again costs nothing and every
	// edge lands on a target pixel.
	//
	// `dp` carries the difference. It resolves against the ratio between the target and the document's
	// authored size, so a 32dp box is the same size on the surface whatever resolution it is rendered at,
	// and PixelsPerUnit stays the only thing deciding how much fits.
	Host->SetSize(TargetSize);

	const float DpRatio = float(TargetSize.Y) / float(FMath::Max(1, Document.Y));

	if (!Host->Update(DeltaTime, DpRatio, 1.0f))
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
		return false;
	}

	const double Along = FVector::DotProduct(Centre - Origin, Normal) / Slope;
	if (Along < 0.0 || Along > double(Distance))
	{
		return false;
	}

	// Right and Up are half extents, so these land in -1 to 1 across the surface.
	const FVector Local = (Origin + Ray * Along) - Centre;
	const double U = FVector::DotProduct(Local, Right) / Right.SizeSquared();
	const double V = FVector::DotProduct(Local, Up) / Up.SizeSquared();
	if (FMath::Abs(U) > 1.0 || FMath::Abs(V) > 1.0)
	{
		return false;
	}

	// Document space runs down from the top left, and the surface's up axis runs the other way.
	MovePointer(FVector2D(U * 0.5 + 0.5, 0.5 - V * 0.5));
	return true;
}

void UFolioWorldDocument::ClearPointer()
{
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

	if (!MovePointerAlong(ViewLocation, ViewRotation.Vector(), PointerReach))
	{
		ClearPointer();
	}

	// Edges rather than the state: RmlUi wants one press and one release, and a held button polled every
	// frame would otherwise arrive as a stream of clicks.
	const bool bDown = PointerKey.IsValid() && Player->IsInputKeyDown(PointerKey);
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
		SFolioWorldFocus::Attach(Host.Get(), bFocusRequested);
	}
	else
	{
		SFolioWorldFocus::Detach(Host.Get());
	}
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

	const APlayerController* Player = World->GetFirstPlayerController();
	const APlayerCameraManager* Camera = Player ? Player->PlayerCameraManager : nullptr;
	if (!Camera)
	{
		return Document;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	Player->GetViewportSize(ViewportX, ViewportY);
	if (ViewportY <= 0)
	{
		return Document;
	}

	const FVector Centre = GetComponentLocation();
	const float Distance = float(FVector::Dist(Centre, Camera->GetCameraLocation()));
	const float HalfFovRadians = FMath::DegreesToRadians(FMath::Clamp(Camera->GetFOVAngle(), 1.0f, 170.0f) * 0.5f);
	const float HalfHeightAtDistance = FMath::Max(1.0f, Distance * FMath::Tan(HalfFovRadians));

	// How tall the surface is on screen, in pixels. The vertical axis because that is what FOV describes
	// once the aspect ratio is applied, and the surface is square-ish enough that one axis decides both.
	const float WorldHeight = float(WorldSize.Y * GetComponentScale().Y);
	const float ScreenPixels = (WorldHeight * 0.5f / HalfHeightAtDistance) * float(ViewportY);

	// Rounded up to a step, so approaching the surface raises the resolution before it looks soft rather
	// than after. Never above the document's own size times the cap, and never above what it lands at.
	const float Ratio = FMath::Clamp(ScreenPixels / float(FMath::Max(1, Document.Y)), 0.25f, MaxSurfaceScale);
	const int32 Height = FMath::Clamp(
		FMath::DivideAndRoundUp(FMath::RoundToInt(float(Document.Y) * Ratio), SurfaceSizeStep) * SurfaceSizeStep,
		SurfaceSizeStep, MaxEdge);

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

	// Replaced rather than resized: resizing a render target in place reallocates its resource anyway, and a
	// new object is what tells a material holding the old one that it has to rebind.
	if (!SurfaceTarget.IsValid() || SurfaceSize != Size)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8;

		// Premultiplied transparent, matching the layer path: a surface that starts opaque black fringes
		// every edge the material later blends.
		Target->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		Target->bAutoGenerateMips = false;
		Target->bForceLinearGamma = false;
		Target->InitAutoFormat(Size.X, Size.Y);
		Target->UpdateResourceImmediate(true);

		SurfaceTarget.Reset(Target);
		SurfaceSize = Size;
		OnSurfaceChanged.Broadcast(Target);
	}

	if (!Renderer->BeginSurface(Size, SurfaceTarget.Get()))
	{
		return;
	}

	Context->Render();
	Renderer->EndSurface();
}

void UFolioWorldDocument::LoadDocument(const FString& InDocumentPath)
{
	DocumentPath = InDocumentPath;

	if (Host.IsValid())
	{
		Host->LoadDocument(InDocumentPath);
	}
}

bool UFolioWorldDocument::BindViewModel(const FName ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (ModelName.IsNone() || !Source)
	{
		return false;
	}

	// Recorded rather than only applied, so a re-register - which moving the component in the editor
	// causes - puts the same models back.
	const int32 Existing = Bindings.IndexOfByPredicate(
		[ModelName](const FPendingBinding& Binding) { return Binding.ModelName == ModelName; });

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
			*ModelName.ToString(), *DocumentPath);

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
		if (Source && !Host->HasObjectModel(Binding.ModelName.ToString()))
		{
			Host->BindObjectModel(Binding.ModelName.ToString(), Source, Binding.bPollEveryFrame);
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

	if (!Host.IsValid())
	{
		return;
	}

	const FName Property = PropertyChangedEvent.GetPropertyName();

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

#include "SFolioCanvas.h"

#include "FolioInsights.h"

#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioInput.h"
#include "FolioUiSounds.h"
#include "FolioRmlUi.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/InputSettings.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	TArray<SFolioCanvas*> LiveCanvases;
}

const TArray<SFolioCanvas*>& SFolioCanvas::GetLiveCanvases()
{
	return LiveCanvases;
}

void SFolioCanvas::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;

	LiveCanvases.AddUnique(this);
	SetCanTick(true);
	SetInteractive(InArgs._Interactive);
	SetScale(InArgs._ScaleFollow, InArgs._Scale, InArgs._MinScale, InArgs._MaxScale);

	SetSupersample(InArgs._Supersample);

	// A player who swaps between the pad and the keyboard expects the document to be ready for
	// whichever they just picked up, without a press that only serves to wake it.
	DeviceChanged = FFolioModule::Get().OnInputDeviceChanged().AddSPLambda(this,
		[this](Folio::EInputDevice) { FocusFirstElement(); });

	// A context needs a size before it will lay anything out, and the real one only arrives with the
	// first geometry. Starting at the requested desired size means a document loaded now is laid out
	// sensibly rather than into a zero-sized viewport.
	ContextSize = FIntPoint(FMath::RoundToInt(DesiredSize.X), FMath::RoundToInt(DesiredSize.Y));

	if (!Host.Create(TEXT("screen"), ContextSize))
	{
		return;
	}

	// Sounds are the canvas's rather than the host's: a world-space document has no UI audio of its own
	// and would not want the same events. The listener holds the document, so it lets go while the
	// document is still alive and attaches to the new one afterwards.
	Host.OnDocumentClosing = [this] { Sounds.Detach(); };
	Host.OnDocumentLoaded = [this](Rml::ElementDocument* Loaded) { Sounds.Attach(Loaded); };

	// Slate caches draw elements for a non-volatile widget inside an invalidation root, so a paint has
	// to be asked for rather than assumed. Outside one this costs nothing and Slate paints regardless.
	Host.OnDirty = [this] { Invalidate(EInvalidateWidgetReason::Paint); };

	if (!InArgs._DocumentPath.IsEmpty())
	{
		Host.LoadDocument(InArgs._DocumentPath);
	}
}

SFolioCanvas::~SFolioCanvas()
{
	LiveCanvases.RemoveSingleSwap(this);
	FFolioModule::Get().OnInputDeviceChanged().Remove(DeviceChanged);
	Sounds.Detach();
	Host.Destroy();
}

void SFolioCanvas::SetInteractive(const bool bInInteractive)
{
	bInteractive = bInInteractive;

	// Hit-test invisible rather than merely unfocusable: the mouse has to reach the game behind a HUD
	// as well, and a leaf widget that answers a hit test still swallows a click it does nothing with.
	SetVisibility(bInteractive ? EVisibility::Visible : EVisibility::HitTestInvisible);

	if (!bInteractive && FSlateApplication::IsInitialized())
	{
		// Whatever focus it already had has to go, or the widget stays the focus target until
		// something else claims it and the game keeps missing input.
		FSlateApplication::Get().ForEachUser([this](FSlateUser& User)
		{
			if (User.GetFocusedWidget().Get() == this)
			{
				User.ClearFocus(EFocusCause::SetDirectly);
			}
		});
	}
}

void SFolioCanvas::LoadDocument(const FString& RelativePath)
{
	Host.LoadDocument(RelativePath);
}

void SFolioCanvas::ReloadDocument()
{
	Host.ReloadDocument();
}

void SFolioCanvas::ReloadStyleSheets()
{
	Host.ReloadStyleSheets();
}

void SFolioCanvas::MarkDirty(const TCHAR* Reason)
{
	Host.MarkDirty(Reason);
}

bool SFolioCanvas::BindObjectModel(const FString& ModelName, UObject* Source, const bool bPollEveryFrame)
{
	return Host.BindObjectModel(ModelName, Source, bPollEveryFrame);
}

bool SFolioCanvas::RouteGamepadKeyDown(const FKeyEvent& KeyEvent)
{
	return OnKeyDown(FGeometry(), KeyEvent).IsEventHandled();
}

bool SFolioCanvas::RouteGamepadKeyUp(const FKeyEvent& KeyEvent)
{
	return OnKeyUp(FGeometry(), KeyEvent).IsEventHandled();
}

bool SFolioCanvas::RouteGamepadAnalog(const FAnalogInputEvent& AnalogEvent)
{
	return OnAnalogValueChanged(FGeometry(), AnalogEvent).IsEventHandled();
}







void SFolioCanvas::SetScale(const float InFollow, const float InScale, const float InMinScale,
	const float InMaxScale)
{
	ScaleFollow = InFollow;
	Scale = InScale;
	MinScale = InMinScale;
	MaxScale = InMaxScale;
}

void SFolioCanvas::GetScaleReport(FIntPoint& OutContextSize, float& OutDpRatio,
	float& OutContextToLocal) const
{
	const FGeometry& Geometry = GetTickSpaceGeometry();
	OutContextSize = ContextSize;
	OutDpRatio = ScaleFor(Geometry);
	OutContextToLocal = ContextToLocal(Geometry);
}

void SFolioCanvas::SetSupersample(const int32 InSamples)
{
	const int32 Wanted = FMath::Clamp(InSamples, 1, 4);
	if (Wanted == Supersample)
	{
		return;
	}

	Supersample = Wanted;

	// Dropped rather than resized: the next paint makes one the right size, and holding a target that
	// nothing is going to draw into is a target the pool cannot have back.
	Target.Reset();
	TargetBrush = FSlateBrush();
}

bool SFolioCanvas::SupersamplingNow() const
{
	return Supersample > 1;
}

float SFolioCanvas::ContextToLocal(const FGeometry& AllottedGeometry) const
{
	// RmlUi has already turned dp into context units by the time anything is drawn, so a context unit
	// is a pixel and Slate must not scale it again. Applying Slate's scale on top is what squares the
	// ratio: a 452dp card lands at 452 x scale x scale pixels, which is right at 1080p and wrong
	// everywhere else.
	return AllottedGeometry.Scale > KINDA_SMALL_NUMBER ? 1.0f / AllottedGeometry.Scale : 1.0f;
}

float SFolioCanvas::ScaleFor(const FGeometry& AllottedGeometry) const
{
	// Times the sample count: the document is laid out into a bigger context and has to fill it, or
	// supersampling would shrink everything rather than sharpening it.
	return Folio::ScaleRatio(AllottedGeometry.Scale, ScaleFollow, Scale, MinScale, MaxScale)
		* float(Supersample);
}

void SFolioCanvas::UpdateContextSize(const FGeometry& AllottedGeometry)
{
	// Pixels, because that is what a context unit is. The dp ratio is carried separately and RmlUi
	// applies it to dp lengths itself.
	const FVector2f Pixels = AllottedGeometry.GetLocalSize() * AllottedGeometry.Scale * float(Supersample);

	ContextSize = FIntPoint(FMath::Max(1, FMath::RoundToInt(Pixels.X)),
		FMath::Max(1, FMath::RoundToInt(Pixels.Y)));
	Host.SetSize(ContextSize);
}

void SFolioCanvas::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!Host.IsValid())
	{
		return;
	}

	UpdateContextSize(AllottedGeometry);

	// Before the context update, so a value read this frame lays out this frame.
	Host.TickDataModels();

	// A held stick becomes repeated presses here, where there is a clock. Suppressed while a text field
	// has focus, for the same reason letters are not directions there.
	if (!IsTakingText())
	{
		if (const Rml::Input::KeyIdentifier Nav = Analog.Advance(InDeltaTime);
			Nav != Rml::Input::KI_UNKNOWN)
		{
			SendNavigation(Nav, 0);
		}
	}

	// Slate's layout scale is Unreal's UI scaling, which is what dp units resolve against. The render
	// scale is separate: it is how much one rendered pixel is worth in document units.
	const float RenderScale =
		AllottedGeometry.GetAccumulatedRenderTransform().GetMatrix().GetScale().GetVector().GetMax();

	Host.Update(InDeltaTime, ScaleFor(AllottedGeometry), RenderScale * ContextToLocal(AllottedGeometry));
}


FVector2D SFolioCanvas::ComputeDesiredSize(float) const
{
	return DesiredSize;
}

int32 SFolioCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_Render);
	SCOPE_CYCLE_COUNTER(STAT_FolioRender);

	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return LayerId;
	}

	FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface();
	if (!Renderer)
	{
		return LayerId;
	}

	/*
	 * Supersampled: the document is drawn into a target of its own and that target is drawn to the
	 * screen, filtered down. Four samples an edge instead of one, which is the only thing that helps
	 * a shallow diagonal.
	 */
	if (SupersamplingNow())
	{
		if (!Target.IsValid() || Target->SizeX != ContextSize.X || Target->SizeY != ContextSize.Y)
		{
			UTextureRenderTarget2D* Fresh = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
			Fresh->RenderTargetFormat = RTF_RGBA8;
			Fresh->ClearColor = FLinearColor::Transparent;
			Fresh->bAutoGenerateMips = false;
			Fresh->InitAutoFormat(ContextSize.X, ContextSize.Y);
			Fresh->UpdateResourceImmediate(true);

			Target.Reset(Fresh);

			// The brush caches its resource, so it is rebuilt with the target rather than repointed.
			TargetBrush = FSlateBrush();
			TargetBrush.SetResourceObject(Fresh);
			TargetBrush.ImageSize = FVector2D(ContextSize);
			TargetBrush.DrawAs = ESlateBrushDrawType::Image;

		}

		// Every paint, on the game thread. A handle to a render target goes stale when the resource
		// behind it is recreated - which a resize, a device loss or a level change all do - and a
		// stale one draws nothing at all. Resolving a brush that has not changed is a lookup.
		TargetHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(TargetBrush);

		if (!TargetHandle.IsValid())
		{
			// Better a hard edge than an empty screen.
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogFolio, Warning,
					TEXT("supersampling has no drawable handle for its render target, so the document "
						"is being drawn directly. Set Supersample to 1 to silence this."));
			}
		}

		if (TargetHandle.IsValid() && Renderer->BeginSurface(ContextSize, Target.Get()))
		{
			Context->Render();
			Renderer->EndSurface();

			const FVector2f Size = AllottedGeometry.GetLocalSize();
			const FSlateRenderTransform& ToWindow = AllottedGeometry.GetAccumulatedRenderTransform();

			const FVector2f Corners[4] = {
				FVector2f(0.0f, 0.0f), FVector2f(Size.X, 0.0f),
				FVector2f(Size.X, Size.Y), FVector2f(0.0f, Size.Y),
			};

			TArray<FSlateVertex> Quad;
			Quad.SetNumUninitialized(4);

			for (int32 Index = 0; Index < 4; ++Index)
			{
				FSlateVertex& Vertex = Quad[Index];
				Vertex = FSlateVertex();
				Vertex.Position = FVector2f(TransformPoint(ToWindow, Corners[Index]));
				Vertex.TexCoords[0] = Index == 1 || Index == 2 ? 1.0f : 0.0f;
				Vertex.TexCoords[1] = Index >= 2 ? 1.0f : 0.0f;
				Vertex.TexCoords[2] = 1.0f;
				Vertex.TexCoords[3] = 1.0f;
				Vertex.MaterialTexCoords = FVector2f(Vertex.TexCoords[0], Vertex.TexCoords[1]);
				Vertex.Color = FColor(255, 255, 255, 255);
			}

			static const TArray<SlateIndex> Indices = { 0, 1, 2, 0, 2, 3 };

			// Custom vertices rather than a box, which resolves the target on the render thread and
			// asks there for a frame time it has no business knowing. Premultiplied, because that is
			// how the document composited into the target.
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, TargetHandle, Quad, Indices,
				nullptr, 0, 0, ESlateDrawEffect::PreMultipliedAlpha);

			return LayerId + 1;
		}
	}

	// RmlUi emits geometry in context units, and the context is sized in dp rather than in Slate's
	// local units. Those agree only while dp follows Slate's own scale, so the draw goes through a
	// child geometry that carries the difference. Without it a document lands short of the bottom
	// right on a window Slate scales down, and past it on one Slate scales up.
	const FGeometry Paint = AllottedGeometry.MakeChild(FVector2D(ContextSize),
		FSlateLayoutTransform(ContextToLocal(AllottedGeometry)));

	Renderer->BeginFrame(OutDrawElements, Paint, LayerId, ContextSize);
	Context->Render();
	Renderer->EndFrame();

	return FMath::Max(LayerId, Renderer->GetMaxLayerId());
}

//
// Input
//

FVector2f SFolioCanvas::ToContextSpace(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	// The context is sized in pixels, and Slate's local space is pixels over Slate's own scale.
	const FVector2f Local = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	return Local * MyGeometry.Scale * float(Supersample);
}

FReply SFolioCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return FReply::Unhandled();
	}

	// Only a real move counts. Slate resends the cursor position on its own, and a stationary
	// mouse would otherwise take the device back from the gamepad every frame.
	if (!MouseEvent.GetCursorDelta().IsNearlyZero())
	{
		NoteDevice(Folio::EInputDevice::Mouse);
	}

	const FVector2f Local = ToContextSpace(MyGeometry, MouseEvent);
	const FIntPoint Position(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y));
	const int32 Modifiers = Folio::ToRmlModifiers(MouseEvent.GetModifierKeys());

	// A viewport with the mouse captured delivers a move event every frame whether the cursor moved or
	// not, so this is what decides whether a document can ever be idle. Compared in the coordinates
	// RmlUi would receive rather than by cursor delta, because a document moving under a stationary
	// cursor is a real move and a stationary cursor over a moving document is not a new event.
	if (LastMousePosition.IsSet() && LastMousePosition.GetValue() == Position
		&& LastMouseModifiers == Modifiers)
	{
		return FReply::Unhandled();
	}

	LastMousePosition = Position;
	LastMouseModifiers = Modifiers;

	MarkDirty(TEXT("mouse move"));
	Context->ProcessMouseMove(Position.X, Position.Y, Modifiers);

	return FReply::Unhandled();
}

FReply SFolioCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("mouse down"));

	const int32 Button = Folio::ToRmlMouseButton(MouseEvent.GetEffectingButton());
	Rml::Context* Context = Host.GetContext();
	if (!Context || Button < 0)
	{
		return FReply::Unhandled();
	}

	NoteDevice(Folio::EInputDevice::Mouse);

	// RmlUi resolves a click against the element under the cursor, and it only knows where
	// that is from the last move. A click without a preceding move over the widget - which is
	// what a synthetic or teleported cursor produces - would otherwise hit whatever was last
	// hovered.
	const FVector2f Local = ToContextSpace(MyGeometry, MouseEvent);
	const int32 Modifiers = Folio::ToRmlModifiers(MouseEvent.GetModifierKeys());
	Context->ProcessMouseMove(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y), Modifiers);
	Context->ProcessMouseButtonDown(Button, Modifiers);

	/*
	 * A right click, as its own event.
	 *
	 * RmlUi has no context menu event, and the button is a parameter of `mousedown` - which a data
	 * expression cannot read, so `data-event-mousedown` fires for the left button as well and there
	 * is no way to tell them apart in markup. `rightclick` and `middleclick` give the other two
	 * buttons somewhere to bind that a left click will never reach.
	 */
	if (Button == 1 || Button == 2)
	{
		if (Rml::Element* Hovered = Context->GetHoverElement())
		{
			Hovered->DispatchEvent(Button == 1 ? "rightclick" : "middleclick", Rml::Dictionary());
		}
	}

	// Capturing keeps the release paired with the press even if the cursor leaves, and taking
	// keyboard focus is what lets a text field receive characters.
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SFolioCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("mouse up"));

	const int32 Button = Folio::ToRmlMouseButton(MouseEvent.GetEffectingButton());
	Rml::Context* Context = Host.GetContext();
	if (!Context || Button < 0)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = ToContextSpace(MyGeometry, MouseEvent);
	const int32 Modifiers = Folio::ToRmlModifiers(MouseEvent.GetModifierKeys());
	Context->ProcessMouseMove(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y), Modifiers);
	Context->ProcessMouseButtonUp(Button, Modifiers);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SFolioCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("mouse wheel"));

	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return FReply::Unhandled();
	}

	// RmlUi scrolls by lines with positive meaning down, which is the opposite sign to Slate.
	const bool bHandled = !Context->ProcessMouseWheel(-MouseEvent.GetWheelDelta(),
		Folio::ToRmlModifiers(MouseEvent.GetModifierKeys()));

	return bHandled ? FReply::Handled() : FReply::Unhandled();
}

void SFolioCanvas::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("mouse leave"));

	if (Rml::Context* Context = Host.GetContext())
	{
		// Without this the last hovered element keeps :hover indefinitely.
		Context->ProcessMouseLeave();
	}

	SLeafWidget::OnMouseLeave(MouseEvent);
}

FReply SFolioCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("key down"));

	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return FReply::Unhandled();
	}

	NoteDevice(Folio::DeviceForKey(KeyEvent.GetKey()));

	const int32 Modifiers = Folio::ToRmlModifiers(KeyEvent.GetModifierKeys());
	const bool bClaimed = ClaimsKey(KeyEvent.GetKey());

	// The literal key first, so a text field still receives W as a letter. Only when nothing is
	// taking text does a letter get to mean a direction, which is what lets the arrows, WASD and
	// IJKL all navigate at once without any of them breaking typing.
	if (const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
		Key != Rml::Input::KI_UNKNOWN)
	{
		// An arrow with navigation switched off still belongs to a text field, and nowhere else.
		const bool bArrow = Key == Rml::Input::KI_UP || Key == Rml::Input::KI_DOWN
			|| Key == Rml::Input::KI_LEFT || Key == Rml::Input::KI_RIGHT;
		if (bArrow && !Folio::ArrowsNavigate() && !IsTakingText())
		{
			return FReply::Unhandled();
		}

		const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);

		// Escape falls through by default, because it is how a document that has gone wrong gets
		// closed and how PIE gets stopped. A game that has bound the back action has said it will
		// handle it, so it stops here - otherwise closing a page and ending the session are the same
		// keystroke.
		bool bTookBack = false;
		if (!bConsumed && Key == Rml::Input::KI_ESCAPE && OnBack.IsBound())
		{
			OnBack.Execute();
			bTookBack = true;
		}

		return bConsumed || bClaimed || bTookBack ? FReply::Handled() : FReply::Unhandled();
	}

	if (IsTakingText())
	{
		return FReply::Unhandled();
	}

	int32 Extra = 0;
	if (const Rml::Input::KeyIdentifier Nav = Folio::ToNavigationKey(KeyEvent.GetKey(), Extra);
		Nav != Rml::Input::KI_UNKNOWN)
	{
		SendNavigation(Nav, Modifiers | Extra);
		return FReply::Handled();
	}

	return bClaimed ? FReply::Handled() : FReply::Unhandled();
}

bool SFolioCanvas::ClaimsKey(const FKey& Key) const
{
	// Two keys always escape, whatever has focus. The console is how a document that has gone wrong
	// gets closed, and Escape is how PIE gets stopped when the console cannot be opened.
	if (Key == EKeys::Escape)
	{
		return false;
	}

	if (const UInputSettings* Settings = UInputSettings::GetInputSettings())
	{
		if (Settings->ConsoleKeys.Contains(Key))
		{
			return false;
		}
	}

	// A focused text field owns the keyboard outright: a letter is a letter, not a movement key.
	// RmlUi delivers characters through ProcessTextInput rather than ProcessKeyDown, so the press
	// itself reads as unconsumed and would otherwise walk the character while a name is typed.
	if (IsTakingText())
	{
		return true;
	}

	// Otherwise only navigation. RmlUi reports a direction it could not act on - already at the edge
	// of a list - as unconsumed, and letting that through turns the pawn behind the menu. Worse, in a
	// CommonUI project the game receiving input makes the action router reapply its input config,
	// which reassigns focus away from the document, and then no further input arrives at all.
	return Folio::ActionForKey(Key) != EFolioNavAction::None;
}

FReply SFolioCanvas::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& AnalogEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("analog"));

	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return FReply::Unhandled();
	}

	// Only recorded here. A stick reports a position every frame, so turning it into presses is
	// Tick's job, where there is a clock to measure the repeat against.
	Analog.SetAxis(AnalogEvent.GetKey(), AnalogEvent.GetAnalogValue());

	if (FMath::Abs(AnalogEvent.GetAnalogValue()) >= Folio::FAnalogNavigator::Deadzone)
	{
		NoteDevice(Folio::EInputDevice::Gamepad);
	}

	return FReply::Unhandled();
}

FReply SFolioCanvas::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& FocusEvent)
{
	MarkDirty(TEXT("focus received"));
	FocusFirstElement();

	return SLeafWidget::OnFocusReceived(MyGeometry, FocusEvent);
}

void SFolioCanvas::FocusFirstElement()
{
	Rml::Context* Context = Host.GetContext();
	Rml::ElementDocument* Document = Host.GetDocument();

	if (!bInteractive || !Context || !Document)
	{
		return;
	}

	// Only when nothing in the document holds focus. A player halfway down a list who picks up the
	// pad has not asked to start again, and a text field being typed into least of all.
	const Rml::Element* Focus = Context->GetFocusElement();
	if (Focus && Focus != Document && Focus != Context->GetRootElement())
	{
		return;
	}

	// Visibly, unless the player is on a mouse: the ring is how anyone navigating knows where they
	// are, and a document that focuses invisibly reads as one that has not focused at all.
	const bool bVisible = FFolioModule::Get().GetInputDevice() != Folio::EInputDevice::Mouse;

	if (Rml::Element* First = Document->FindNextTabElement(Document, /*forward*/ true))
	{
		First->Focus(bVisible);
		MarkDirty(TEXT("first focus"));
	}
}

void SFolioCanvas::OnFocusLost(const FFocusEvent& FocusEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("focus lost"));

	// A stick held while focus moves elsewhere would otherwise keep repeating into this document.
	Analog.Reset();
	SLeafWidget::OnFocusLost(FocusEvent);
}

bool SFolioCanvas::SendNavigation(const Rml::Input::KeyIdentifier Key, const int32 Modifiers)
{
	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return false;
	}

	// Down and up together. RmlUi navigates on the press, but an element left thinking a key is
	// still held misbehaves the next time the same key arrives.
	const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);
	Context->ProcessKeyUp(Key, Modifiers);

	// Nothing in the document wanted the back action, so the game gets told. A modal that closed
	// itself has consumed it and this does not fire.
	if (!bConsumed && Key == Rml::Input::KI_ESCAPE)
	{
		OnBack.ExecuteIfBound();
	}

	return bConsumed;
}


Folio::EInputDevice SFolioCanvas::GetLastInputDevice() const
{
	return FFolioModule::Get().GetInputDevice();
}

void SFolioCanvas::NoteDevice(const Folio::EInputDevice Device)
{
	// Held by the module, not here: two documents open at once must not disagree about whether to
	// draw a keyboard prompt, and only one of them receives the key that changed it.
	FFolioModule::Get().SetInputDevice(Device);
}


FReply SFolioCanvas::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("key up"));

	Rml::Context* Context = Host.GetContext();
	const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
	if (!Context || Key == Rml::Input::KI_UNKNOWN)
	{
		// Paired with the press: a release reaching the game after its press did not leaves
		// Enhanced Input holding an action down that nothing will ever complete.
		return ClaimsKey(KeyEvent.GetKey()) ? FReply::Handled() : FReply::Unhandled();
	}

	Context->ProcessKeyUp(Key, Folio::ToRmlModifiers(KeyEvent.GetModifierKeys()));
	return FReply::Handled();
}

FReply SFolioCanvas::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& CharacterEvent)
{
	// Input changes something by definition, even when RmlUi decides not to act on it: a focus ring,
	// a hover state, a caret position. Marked here rather than beside each Process call so a handler
	// added later cannot forget.
	MarkDirty(TEXT("text input"));

	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return FReply::Unhandled();
	}

	const TCHAR Character = CharacterEvent.GetCharacter();
	if (Character < TEXT(' '))
	{
		// Control characters arrive as key events instead; passing them here would insert
		// them into text fields literally.
		return FReply::Unhandled();
	}

	Context->ProcessTextInput(static_cast<Rml::Character>(Character));
	return FReply::Handled();
}

#include "SFolioCanvas.h"

#include "FolioAccessibility.h"
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

SFolioCanvas::SFolioCanvas()
	: AccessibleChildren(this)
{
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
	LastDevice = FFolioModule::Get().GetInputDevice();
	DeviceChanged = FFolioModule::Get().OnInputDeviceChanged().AddSPLambda(this,
		[this](const Folio::EInputDevice Device) { OnDeviceChanged(Device); });

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
	//
	// The accessibility tree observes the elements of the document being closed, so it has to let go
	// here for the same reason: on the next load those elements are freed.
	Host.OnDocumentClosing = [this]
	{
		Sounds.Detach();

		if (Accessibility.IsValid())
		{
			Accessibility->Clear();
			AccessibleChildren.Empty();
		}
	};
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
	SPanel::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

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

	const bool bUpdated = Host.Update(InDeltaTime, ScaleFor(AllottedGeometry),
		RenderScale * ContextToLocal(AllottedGeometry));

	UpdateAccessibility(bUpdated, AllottedGeometry);
}


FVector2D SFolioCanvas::ComputeDesiredSize(float) const
{
	return DesiredSize;
}

//
// Accessibility. Everything here is inert until a screen reader turns on.
//

FChildren* SFolioCanvas::GetChildren()
{
	return &AccessibleChildren;
}

void SFolioCanvas::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (AccessibleChildren.Num() == 0)
	{
		return;
	}

	// One child, the document itself, filling the canvas. Everything below it is placed by that widget
	// against the offsets the document laid out at.
	const TSharedRef<SWidget>& Root = AccessibleChildren[0];
	if (ArrangedChildren.Accepts(Root->GetVisibility()))
	{
		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Root,
			FVector2D(AllottedGeometry.GetLocalSize()), FSlateLayoutTransform()));
	}
}

void SFolioCanvas::UpdateAccessibility(const bool bDocumentChanged, const FGeometry& AllottedGeometry)
{
	if (!FFolioAccessibilityTree::IsWanted())
	{
		if (Accessibility.IsValid())
		{
			Accessibility.Reset();
			AccessibleChildren.Empty();
			AccessibilityScale = 0.0f;
			Invalidate(EInvalidateWidgetReason::ChildOrder);
		}

		return;
	}

	// Nothing to describe until the document has been through a layout: an element with no computed
	// box is one with no name, no state and no position.
	if (!Host.IsLaidOut())
	{
		return;
	}

	if (!Accessibility.IsValid())
	{
		Accessibility = MakeUnique<FFolioAccessibilityTree>();
		Accessibility->OnChanged = FSimpleDelegate::CreateSP(this, &SFolioCanvas::OnAccessibilityChanged);
	}

	// A context unit is a pixel of the context, and a supersampled context has more of them per Slate
	// unit than the screen does. Proxies are placed on screen, so they follow the screen.
	const float Scale = ContextToLocal(AllottedGeometry) / float(Supersample);

	if (bDocumentChanged || Accessibility->IsEmpty() || Scale != AccessibilityScale)
	{
		// Forced when there is nothing yet or the whole thing has moved, and rate limited otherwise -
		// a document that changed has usually not changed what it means, and the tree decides that.
		const bool bForce = Accessibility->IsEmpty() || Scale != AccessibilityScale;

		AccessibilityScale = Scale;
		Accessibility->Refresh(Host.GetDocument(), Scale, bForce);

		// Only when the root widget itself is a different one. Detaching and reattaching the same
		// widget would tell the platform its parent had changed on every frame the document moved.
		const TSharedPtr<SFolioAccessibleElement> Root = Accessibility->GetRoot();
		const bool bAlreadyThere = AccessibleChildren.Num() == (Root.IsValid() ? 1 : 0)
			&& (!Root.IsValid() || &AccessibleChildren[0].Get() == Root.Get());

		if (!bAlreadyThere)
		{
			AccessibleChildren.Empty();
			if (Root.IsValid())
			{
				AccessibleChildren.Add(Root.ToSharedRef());
			}

			Invalidate(EInvalidateWidgetReason::ChildOrder);
		}
	}

	// Every frame rather than only on a change: focus moves without the document changing shape, and
	// this is what makes navigating a menu say what was navigated onto.
	Accessibility->SyncFocus(Host.GetContext());
	Accessibility->SyncEditing(Host.GetContext());
}

void SFolioCanvas::OnAccessibilityChanged()
{
	MarkDirty(TEXT("screen reader"));
}

void SFolioCanvas::DescribeAccessibility(TArray<FString>& Out) const
{
	if (Accessibility.IsValid())
	{
		Accessibility->Describe(Out);
	}
}

int32 SFolioCanvas::PaintAccessibility(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId,
	const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) const
{
	if (AccessibleChildren.Num() == 0)
	{
		return LayerId;
	}

	// The proxies emit no geometry. This is here because Slate records a widget's bounds when it is
	// painted, and those bounds are what the platform hit tests and what a screen reader's cursor
	// follows: a proxy that is never painted is a control at the origin, one pixel across.
	FArrangedChildren Arranged(EVisibility::All);
	ArrangeChildren(AllottedGeometry, Arranged);

	return PaintArrangedChildren(Args, Arranged, AllottedGeometry, CullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);
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

	// Nothing to draw until it has been laid out once. RmlUi's defaults are not the author's - a body
	// with no background rule computed yet is opaque white - so painting a document on the frame it
	// loaded would be a frame of the wrong thing.
	if (!Host.IsLaidOut())
	{
		return LayerId;
	}

	// A diagnostic for a white frame as a screen opens. Slate ticks before it paints, so a document
	// is always laid out by its first paint and the guard above cannot be what is showing; if
	// dropping the first painted frames removes the flash it is the content of those frames, and if
	// it does not the flash is not coming from this widget at all.
	{
		static const IConsoleVariable* SkipVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("folio.SkipFirstPaints"));

		const int32 Skip = SkipVar ? SkipVar->GetInt() : 0;
		if (Skip > 0 && PaintsSoFar < Skip)
		{
			++PaintsSoFar;
			return LayerId;
		}
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

			PaintAccessibility(Args, AllottedGeometry, CullingRect, OutDrawElements, LayerId,
				InWidgetStyle, bParentEnabled);

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

	PaintAccessibility(Args, AllottedGeometry, CullingRect, OutDrawElements, LayerId, InWidgetStyle,
		bParentEnabled);

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

	// Only a real move counts, and taking the device back from a pad takes a deliberate one.
	//
	// A stationary mouse resends its position every frame, and a game running CommonUI's analog
	// cursor has the stick driving the pointer as well, so a single non-zero delta is not evidence
	// that anyone touched a mouse. Distance since the last pad input is. Any gamepad key resets it.
	if (const FVector2f Delta = MouseEvent.GetCursorDelta(); !Delta.IsNearlyZero())
	{
		if (FFolioModule::Get().GetInputDevice() == Folio::EInputDevice::Gamepad)
		{
			MouseTravel += Delta.Size();
			if (MouseTravel >= 24.0f)
			{
				NoteDevice(Folio::EInputDevice::Mouse);
			}
		}
		else
		{
			NoteDevice(Folio::EInputDevice::Mouse);
		}
	}

	// The pointer steers only while the pointer is what the player is holding. Anything else and the
	// move is dropped before RmlUi sees it, so no hover can exist on a pad or a keyboard at all:
	// there is one highlight on screen, it is the focus ring, and it is the thing confirm presses.
	// A real move takes the device back a few lines above and this stops applying on the same event.
	if (FFolioModule::Get().GetInputDevice() != Folio::EInputDevice::Mouse)
	{
		return FReply::Unhandled();
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

FReply SFolioCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnMouseButtonDown(MyGeometry, MouseEvent);
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

	SPanel::OnMouseLeave(MouseEvent);
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

	// A pad press means the pointer is not what is being used, whatever the cursor has been doing.
	if (KeyEvent.GetKey().IsGamepadKey())
	{
		MouseTravel = 0.0f;
	}

	NoteDevice(Folio::DeviceForKey(KeyEvent.GetKey()));

	const int32 Modifiers = Folio::ToRmlModifiers(KeyEvent.GetModifierKeys());
	const bool bClaimed = ClaimsKey(KeyEvent.GetKey());

	// The literal key first, so a text field still receives W as a letter. Only when nothing is
	// taking text does a letter get to mean a direction, which is what lets the arrows, WASD and
	// IJKL all navigate at once without any of them breaking typing.
	// Before anything RmlUi might do with it: the game asked for this key outright.
	if (GameKeys.Contains(KeyEvent.GetKey()))
	{
		OnUnclaimedKey.ExecuteIfBound(KeyEvent.GetKey());
		return FReply::Handled();
	}

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

	if (!bClaimed)
	{
		OnUnclaimedKey.ExecuteIfBound(KeyEvent.GetKey());
	}

	return bClaimed ? FReply::Handled() : FReply::Unhandled();
}

bool SFolioCanvas::ClaimsKey(const FKey& Key) const
{
	if (GameKeys.Contains(Key))
	{
		return true;
	}

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

	return SPanel::OnFocusReceived(MyGeometry, FocusEvent);
}

void SFolioCanvas::OnDeviceChanged(const Folio::EInputDevice Device)
{
	const Folio::EInputDevice Previous = LastDevice;
	LastDevice = Device;

	Rml::Context* Context = Host.GetContext();
	Rml::ElementDocument* Document = Host.GetDocument();

	if (!bInteractive || !Context || !Document)
	{
		return;
	}

	if (Device != Folio::EInputDevice::Mouse)
	{
		// The cursor has not moved, so RmlUi still holds a hover chain and the element under the
		// pointer keeps its highlight. Two lit controls and only one of them is the one a confirm
		// will press.
		//
		// The last position is kept deliberately. Clearing it would let the next resend of a
		// stationary cursor through the move filter, and RmlUi would put the hover straight back on
		// the element this just took it off. Only a move to somewhere else brings it back.
		Context->ProcessMouseLeave();
	}

	// Coming off the mouse, whatever holds focus was focused by a click, which means focused
	// invisibly. Re-focusing it visibly needs a blur first: RmlUi sends no focus event when the
	// element is already the focused one, and the visible flag rides on that event.
	if (Previous == Folio::EInputDevice::Mouse && Device != Folio::EInputDevice::Mouse)
	{
		Rml::Element* Focused = Context->GetFocusElement();

		const bool bReal = Focused && Focused != Document && Focused != Context->GetRootElement();

		// Not a field being typed into. Blurring one to light its ring would move the caret, and a
		// player typing already knows where they are.
		const bool bTyping = bReal
			&& (Focused->GetTagName() == "input" || Focused->GetTagName() == "textarea");

		if (bReal && !bTyping)
		{
			Document->Focus();
			Focused->Focus(true);
		}
	}

	if (Previous == Folio::EInputDevice::Mouse || Previous == Folio::EInputDevice::Unknown)
	{
		FocusFirstElement();
	}

	MarkDirty(TEXT("input device changed"));
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
	SPanel::OnFocusLost(FocusEvent);
}

bool SFolioCanvas::SendNavigation(const Rml::Input::KeyIdentifier Key, const int32 Modifiers)
{
	Rml::Context* Context = Host.GetContext();
	if (!Context)
	{
		return false;
	}

	// Navigating is a statement that the mouse is not what is steering. RmlUi recomputes the hover
	// chain only when the pointer moves, so without this the last hovered element keeps its
	// highlight for as long as the cursor sits still, even once the page it belonged to is hidden.
	// Two lit controls, and the stale one is the one that catches the eye.
	Context->ProcessMouseLeave();

	// Down and up together. RmlUi navigates on the press, but an element left thinking a key is
	// still held misbehaves the next time the same key arrives.
	const Rml::Element* Before = Context->GetFocusElement();

	const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);
	Context->ProcessKeyUp(Key, Modifiers);

	static const IConsoleVariable* TraceVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("folio.Input.Trace"));

	if (TraceVar && TraceVar->GetInt() != 0)
	{
		const Rml::Element* After = Context->GetFocusElement();
		UE_LOG(LogFolio, Display, TEXT("input trace: nav %d focus %s -> %s%s"),
			static_cast<int32>(Key),
			Before ? *Folio::ToFString(Before->GetAddress(false, false)) : TEXT("none"),
			After ? *Folio::ToFString(After->GetAddress(false, false)) : TEXT("none"),
			bConsumed ? TEXT(" consumed") : TEXT(""));
	}

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

#include "SFolioWorldFocus.h"

#include "FolioDocumentHost.h"
#include "FolioInput.h"
#include "FolioModule.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	TSharedPtr<SFolioWorldFocus> Sink;
	FFolioDocumentHost* SinkHost = nullptr;
}

void SFolioWorldFocus::Construct(const FArguments&, FFolioDocumentHost* InHost, const bool bInNavigates)
{
	Host = InHost;
	bNavigates = bInNavigates;

	// A stick reports a position rather than an event, so the repeat needs a clock.
	SetCanTick(true);
	SetVisibility(EVisibility::Visible);
}

void SFolioWorldFocus::Attach(FFolioDocumentHost* InHost, const bool bNavigates)
{
	if (!InHost || !GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// Already up for this document, but the level of focus changed: a text field taking focus inside an
	// already navigable document must not tear the sink down and put it back.
	if (SinkHost == InHost && Sink.IsValid())
	{
		Sink->bNavigates = bNavigates;
		return;
	}

	Detach(SinkHost);

	Sink = SNew(SFolioWorldFocus, InHost, bNavigates);
	SinkHost = InHost;

	GEngine->GameViewport->AddViewportWidgetContent(Sink.ToSharedRef(), TNumericLimits<int32>::Max());
	FSlateApplication::Get().SetKeyboardFocus(Sink, EFocusCause::SetDirectly);
}

void SFolioWorldFocus::Detach(const FFolioDocumentHost* InHost)
{
	if (!Sink.IsValid() || SinkHost != InHost)
	{
		return;
	}

	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Sink.ToSharedRef());
	}

	Sink.Reset();
	SinkHost = nullptr;

	// Handed back explicitly. Removing a focused widget leaves focus nowhere, and the symptom is a game
	// that stops answering the keyboard entirely once a terminal has been used.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocusToGameViewport(0, EFocusCause::SetDirectly);
	}
}

bool SFolioWorldFocus::IsActive()
{
	return Sink.IsValid();
}

void SFolioWorldFocus::ReclaimFocus(FSlateApplication& SlateApp, const int32 UserIndex)
{
	if (!Sink.IsValid())
	{
		return;
	}

	// The console focuses the viewport widget for itself. Taking that back leaves it open with nothing
	// able to be typed into it.
	if (Folio::IsConsoleOpen())
	{
		return;
	}

	if (SlateApp.GetUserFocusedWidget(UserIndex) == Sink)
	{
		return;
	}

	SlateApp.SetUserFocus(UserIndex, Sink, EFocusCause::SetDirectly);
}

bool SFolioWorldFocus::RouteGamepadKeyDown(const FKeyEvent& KeyEvent)
{
	return Sink.IsValid() && Sink->OnKeyDown(FGeometry(), KeyEvent).IsEventHandled();
}

bool SFolioWorldFocus::RouteGamepadKeyUp(const FKeyEvent& KeyEvent)
{
	return Sink.IsValid() && Sink->OnKeyUp(FGeometry(), KeyEvent).IsEventHandled();
}

bool SFolioWorldFocus::RouteGamepadAnalog(const FAnalogInputEvent& AnalogEvent)
{
	return Sink.IsValid() && Sink->OnAnalogValueChanged(FGeometry(), AnalogEvent).IsEventHandled();
}

bool SFolioWorldFocus::IsTakingText() const
{
	return Host && Host->IsTakingText();
}

bool SFolioWorldFocus::SendNavigation(const Rml::Input::KeyIdentifier Key, const int32 Modifiers)
{
	Rml::Context* Context = Host ? Host->GetContext() : nullptr;
	if (!Context)
	{
		return false;
	}

	// Down and up together. RmlUi navigates on the press, but an element left thinking a key is still
	// held misbehaves the next time the same key arrives.
	const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);
	Context->ProcessKeyUp(Key, Modifiers);
	return bConsumed;
}

bool SFolioWorldFocus::ClaimsKey(const FKey& Key) const
{
	// Two keys always escape, whatever has focus. The console is how a document that has gone wrong gets
	// closed, and Escape is how PIE gets stopped when the console cannot be opened.
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

	// A focused text field owns the keyboard outright: a letter is a letter, not a movement key. RmlUi
	// delivers characters through ProcessTextInput rather than ProcessKeyDown, so the press itself reads
	// as unconsumed and would otherwise walk the player while a name is typed.
	if (IsTakingText())
	{
		return true;
	}

	// Otherwise only navigation, and only once the document has been focused outright. A document
	// holding focus for a text field alone must let the player walk away.
	return bNavigates && Folio::ActionForKey(Key) != EFolioNavAction::None;
}

void SFolioWorldFocus::Tick(const FGeometry&, const double, const float DeltaTime)
{
	if (!Host || IsTakingText())
	{
		return;
	}

	if (const Rml::Input::KeyIdentifier Nav = Analog.Advance(DeltaTime); Nav != Rml::Input::KI_UNKNOWN)
	{
		Host->MarkDirty(TEXT("world analog"));
		SendNavigation(Nav, 0);
	}
}

FReply SFolioWorldFocus::OnKeyDown(const FGeometry&, const FKeyEvent& KeyEvent)
{
	// The console owns the keyboard while it is up, whoever holds focus.
	if (Folio::IsConsoleOpen())
	{
		return FReply::Unhandled();
	}

	Rml::Context* Context = Host ? Host->GetContext() : nullptr;
	if (!Context)
	{
		return FReply::Unhandled();
	}

	Host->MarkDirty(TEXT("world key down"));
	FFolioModule::Get().SetInputDevice(Folio::DeviceForKey(KeyEvent.GetKey()));

	const int32 Modifiers = Folio::ToRmlModifiers(KeyEvent.GetModifierKeys());
	const bool bClaimed = ClaimsKey(KeyEvent.GetKey());

	// The literal key first, so a text field still receives W as a letter. Only when nothing is taking
	// text does a letter get to mean a direction.
	if (const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
		Key != Rml::Input::KI_UNKNOWN)
	{
		const bool bArrow = Key == Rml::Input::KI_UP || Key == Rml::Input::KI_DOWN
			|| Key == Rml::Input::KI_LEFT || Key == Rml::Input::KI_RIGHT;
		if (bArrow && !Folio::ArrowsNavigate() && !IsTakingText())
		{
			return FReply::Unhandled();
		}

		const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);
		if (bConsumed || bClaimed)
		{
			Claimed.Add(KeyEvent.GetKey());
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	if (IsTakingText() || !bNavigates)
	{
		return FReply::Unhandled();
	}

	int32 Extra = 0;
	if (const Rml::Input::KeyIdentifier Nav = Folio::ToNavigationKey(KeyEvent.GetKey(), Extra);
		Nav != Rml::Input::KI_UNKNOWN)
	{
		Claimed.Add(KeyEvent.GetKey());
		SendNavigation(Nav, Modifiers | Extra);
		return FReply::Handled();
	}

	if (bClaimed)
	{
		Claimed.Add(KeyEvent.GetKey());
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SFolioWorldFocus::OnKeyUp(const FGeometry&, const FKeyEvent& KeyEvent)
{
	// A release is only ours if the press was. Anything else belongs to whoever took the press, and
	// swallowing it is what strands a held movement key.
	if (Claimed.Remove(KeyEvent.GetKey()) == 0)
	{
		return FReply::Unhandled();
	}

	Rml::Context* Context = Host ? Host->GetContext() : nullptr;
	const Rml::Input::KeyIdentifier Key = Context ? Folio::ToRmlKey(KeyEvent.GetKey()) : Rml::Input::KI_UNKNOWN;
	if (Key != Rml::Input::KI_UNKNOWN)
	{
		Context->ProcessKeyUp(Key, Folio::ToRmlModifiers(KeyEvent.GetModifierKeys()));
	}

	return FReply::Handled();
}

FReply SFolioWorldFocus::OnKeyChar(const FGeometry&, const FCharacterEvent& CharacterEvent)
{
	// The console owns the keyboard while it is up, whoever holds focus.
	if (Folio::IsConsoleOpen())
	{
		return FReply::Unhandled();
	}

	Rml::Context* Context = Host ? Host->GetContext() : nullptr;
	if (!Context)
	{
		return FReply::Unhandled();
	}

	const TCHAR Character = CharacterEvent.GetCharacter();
	if (Character < TEXT(' '))
	{
		// Control characters arrive as key events instead; passing them here would insert them into
		// text fields literally.
		return FReply::Unhandled();
	}

	Host->MarkDirty(TEXT("world text input"));
	Context->ProcessTextInput(static_cast<Rml::Character>(Character));
	return FReply::Handled();
}

FReply SFolioWorldFocus::OnAnalogValueChanged(const FGeometry&, const FAnalogInputEvent& AnalogEvent)
{
	if (!Host || !bNavigates)
	{
		return FReply::Unhandled();
	}

	// Only recorded here. Turning a held stick into presses is Tick's job, where there is a clock.
	Analog.SetAxis(AnalogEvent.GetKey(), AnalogEvent.GetAnalogValue());

	if (FMath::Abs(AnalogEvent.GetAnalogValue()) >= Folio::FAnalogNavigator::Deadzone)
	{
		FFolioModule::Get().SetInputDevice(Folio::EInputDevice::Gamepad);
	}

	return FReply::Unhandled();
}

void SFolioWorldFocus::OnFocusLost(const FFocusEvent& FocusEvent)
{
	// A stick held while focus moves elsewhere would otherwise keep repeating into this document.
	Analog.Reset();

	// Releases now go wherever focus went, so nothing here may still be waiting for them.
	Claimed.Reset();
	SLeafWidget::OnFocusLost(FocusEvent);
}

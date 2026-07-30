#include "SFolioWorldFocus.h"

#include "FolioDocumentHost.h"
#include "FolioInput.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"

#include <RmlUi/Core/Context.h>

namespace
{
	TSharedPtr<SFolioWorldFocus> Sink;
	FFolioDocumentHost* SinkHost = nullptr;
}

void SFolioWorldFocus::Construct(const FArguments&, FFolioDocumentHost* InHost)
{
	Host = InHost;
	SetVisibility(EVisibility::Visible);
}

void SFolioWorldFocus::Attach(FFolioDocumentHost* InHost)
{
	if (!InHost || SinkHost == InHost || !GEngine || !GEngine->GameViewport
		|| !FSlateApplication::IsInitialized())
	{
		return;
	}

	Detach(SinkHost);

	Sink = SNew(SFolioWorldFocus, InHost);
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
	// that stops responding to the keyboard entirely once a terminal has been used.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocusToGameViewport(0, EFocusCause::SetDirectly);
	}
}

FReply SFolioWorldFocus::OnKeyDown(const FGeometry&, const FKeyEvent& KeyEvent)
{
	Rml::Context* Context = Host ? Host->GetContext() : nullptr;
	if (!Context)
	{
		return FReply::Unhandled();
	}

	Host->MarkDirty(TEXT("world key down"));

	const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
	if (Key == Rml::Input::KI_UNKNOWN)
	{
		return FReply::Unhandled();
	}

	const bool bConsumed = !Context->ProcessKeyDown(Key, Folio::ToRmlModifiers(KeyEvent.GetModifierKeys()));
	return bConsumed ? FReply::Handled() : FReply::Unhandled();
}

FReply SFolioWorldFocus::OnKeyUp(const FGeometry&, const FKeyEvent& KeyEvent)
{
	Rml::Context* Context = Host ? Host->GetContext() : nullptr;
	const Rml::Input::KeyIdentifier Key = Context ? Folio::ToRmlKey(KeyEvent.GetKey()) : Rml::Input::KI_UNKNOWN;
	if (Key == Rml::Input::KI_UNKNOWN)
	{
		return FReply::Unhandled();
	}

	Context->ProcessKeyUp(Key, Folio::ToRmlModifiers(KeyEvent.GetModifierKeys()));
	return FReply::Unhandled();
}

FReply SFolioWorldFocus::OnKeyChar(const FGeometry&, const FCharacterEvent& CharacterEvent)
{
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

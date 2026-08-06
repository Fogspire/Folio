#include "FolioUiSounds.h"

#include "FolioModule.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

namespace Folio
{
	const TCHAR* DescribeUiSound(const EUiSound Sound)
	{
		switch (Sound)
		{
		case EUiSound::Focus:   return TEXT("focus");
		case EUiSound::Confirm: return TEXT("confirm");
		case EUiSound::Back:    return TEXT("back");
		default:                return TEXT("invalid");
		}
	}

	bool AreSoundEventsAvailable()
	{
		return FFolioModule::IsAvailable();
	}

	FOnUiSound& OnUiSound()
	{
		return FFolioModule::Get().OnUiSound();
	}

	FOnTextRevealSound& OnTextRevealSound()
	{
		return FFolioModule::Get().OnTextRevealSound();
	}

	void FUiSoundListener::Attach(Rml::ElementDocument* Document)
	{
		Detach();
		if (Document == nullptr)
		{
			return;
		}

		Attached = Document;

		// Captured on the way down, so an element that stops the event still makes its noise, and
		// so a listener on the document sees events from every descendant.
		Attached->AddEventListener(Rml::EventId::Focus, this, /*in_capture_phase*/ true);
		Attached->AddEventListener(Rml::EventId::Click, this, true);
	}

	void FUiSoundListener::Detach()
	{
		if (Attached == nullptr)
		{
			return;
		}

		Attached->RemoveEventListener(Rml::EventId::Focus, this, true);
		Attached->RemoveEventListener(Rml::EventId::Click, this, true);
		Attached = nullptr;
	}

	void FUiSoundListener::ProcessEvent(Rml::Event& Event)
	{
		if (!FFolioModule::IsAvailable())
		{
			return;
		}

		Rml::Element* Target = Event.GetTargetElement();
		if (Target == nullptr)
		{
			return;
		}

		const auto Report = [](const EUiSound Sound)
		{
			FFolioModule::Get().OnUiSound().Broadcast(Sound);
		};

		switch (Event.GetId())
		{
		case Rml::EventId::Focus:
			// The document itself takes focus whenever nothing else has it, which is not a move
			// the player made and not a noise anyone wants.
			if (Target != Attached)
			{
				Report(EUiSound::Focus);
			}
			break;

		case Rml::EventId::Click:
			// A disabled control still receives the click; it just does nothing with it. That is
			// exactly the case a game wants a different sound for.
			Report(Target->IsClassSet("is-disabled") || Target->IsPseudoClassSet("disabled")
				? EUiSound::Invalid
				: EUiSound::Confirm);
			break;

		default:
			break;
		}
	}
}

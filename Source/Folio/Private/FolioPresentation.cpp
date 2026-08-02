#include "FolioPresentation.h"

#include "FolioDocumentHost.h"

#include "FolioCore/FolioTextElement.h"

#include <RmlUi/Core/Context.h>

namespace
{
	struct FPresentationState
	{
		float TextScale = 1.0f;
		bool bReducedMotion = false;

		// From one rather than zero, so that a host which has applied nothing yet always differs from
		// it and a freshly loaded document is scaled on its first update.
		uint32 Revision = 1;
	};

	FPresentationState& PresentationState()
	{
		static FPresentationState Instance;
		return Instance;
	}

	const FName ReducedMotionTheme(TEXT("reduced-motion"));
	const FName HighContrastTheme(TEXT("high-contrast"));
}

void Folio::Presentation::SetTextScale(const float Scale)
{
	// The floor is where the type stops being smaller and starts being unreadable, and the ceiling is
	// where a screen stops being a screen. Both are wider than any setting should offer; they exist so
	// a bad value cannot make the UI unrecoverable.
	const float Clamped = FMath::Clamp(Scale, 0.5f, 3.0f);

	FPresentationState& Presentation = PresentationState();
	if (FMath::IsNearlyEqual(Presentation.TextScale, Clamped))
	{
		return;
	}

	Presentation.TextScale = Clamped;
	++Presentation.Revision;
}

float Folio::Presentation::GetTextScale()
{
	return PresentationState().TextScale;
}

void Folio::Presentation::SetReducedMotion(const bool bReduce)
{
	FPresentationState& Presentation = PresentationState();
	if (Presentation.bReducedMotion == bReduce)
	{
		return;
	}

	Presentation.bReducedMotion = bReduce;
	++Presentation.Revision;

	FFolioDocumentHost::SetTheme(ReducedMotionTheme, bReduce);

	// A stylesheet cannot reach the smooth scroll, because it is not a transition: RmlUi glides a
	// focused item into view from its own controller. A list sliding under the thumb is movement like
	// any other.
	for (FFolioDocumentHost* Host : FFolioDocumentHost::GetLiveHosts())
	{
		ApplyToContext(Host->GetContext());
	}
}

void Folio::Presentation::ApplyToContext(Rml::Context* Context)
{
	if (Context)
	{
		Context->SetDefaultScrollBehavior(
			IsReducedMotionOn() ? Rml::ScrollBehavior::Instant : Rml::ScrollBehavior::Smooth, 1.0f);
	}
}

void Folio::Presentation::SetTextSpeed(const float Multiplier)
{
	// A tenth is unreadably slow and ten is instant in everything but name; both are wider than any
	// setting should offer and exist so a bad value cannot make dialogue unusable.
	Folio::Text::RevealSettings().Speed = FMath::Clamp(Multiplier, 0.1f, 10.0f);
}

float Folio::Presentation::GetTextSpeed()
{
	return Folio::Text::RevealSettings().Speed;
}

void Folio::Presentation::SetInstantText(const bool bInstant)
{
	Folio::Text::RevealSettings().bInstant = bInstant;
}

bool Folio::Presentation::IsInstantTextOn()
{
	return Folio::Text::RevealSettings().bInstant;
}

bool Folio::Presentation::IsReducedMotionOn()
{
	return PresentationState().bReducedMotion;
}

void Folio::Presentation::SetHighContrast(const bool bEnabled)
{
	FFolioDocumentHost::SetTheme(HighContrastTheme, bEnabled);
	++PresentationState().Revision;
}

bool Folio::Presentation::IsHighContrastOn()
{
	return FFolioDocumentHost::IsThemeActive(HighContrastTheme);
}

uint32 Folio::Presentation::GetRevision()
{
	return PresentationState().Revision;
}

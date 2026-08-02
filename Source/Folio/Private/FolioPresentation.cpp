#include "FolioPresentation.h"

#include "FolioDocumentHost.h"

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

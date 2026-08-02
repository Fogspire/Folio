#pragma once

#include "CoreMinimal.h"

/**
 * The settings that change how a screen is presented rather than what it says.
 *
 * Larger text, less movement, higher contrast: three of the things a platform's accessibility
 * questionnaire asks about that are not a screen reader, and all three are properties of the whole UI
 * rather than of one document. They live here so that a game sets one and every document already open
 * answers, including documents opened afterwards - the same contract as a theme.
 *
 * None of them is on by default, and none is inferred from the operating system. A player asked for
 * this or they did not.
 */
namespace Folio::Presentation
{
	/**
	 * How much bigger than authored the type is, 1 being as authored.
	 *
	 * Applied by setting the root font size, so it reaches every rule written in `rem` and no rule
	 * written in `dp`. That is a real constraint rather than an implementation detail, and the reason
	 * the shipped theme sizes its type in `rem`: a scale that silently missed half a screen would be
	 * worse than none, so `uipreview --text-scale` reports the text it cannot move.
	 */
	void SetTextScale(float Scale);
	float GetTextScale();

	/**
	 * Turns transitions and animations off across every document.
	 *
	 * Not a taste setting. Vestibular disorders make large movement genuinely unpleasant, and a menu
	 * that slides, scales and parallaxes is the common trigger. Implemented as a theme,
	 * `reduced-motion`, because that is how a stylesheet is reached from outside it; code-driven
	 * movement reads `IsReducedMotionOn` and snaps instead.
	 */
	void SetReducedMotion(bool bReduce);
	bool IsReducedMotionOn();

	/** The `high-contrast` theme, named so a game does not have to hard-code the string. */
	void SetHighContrast(bool bEnabled);
	bool IsHighContrastOn();

	/**
	 * Bumped whenever any of these changes.
	 *
	 * What lets a document host notice a setting moved without storing a copy of every one of them,
	 * and without a delegate per host.
	 */
	uint32 GetRevision();
}

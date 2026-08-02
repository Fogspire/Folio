#pragma once

#include "FolioCore/FolioSemantics.h"

#include "CoreMinimal.h"

/**
 * Every word the screen reader says that did not come out of a document.
 *
 * "check box", "not checked", "unavailable", the name of the comma key - a reader is about a third
 * its own vocabulary, and all of it has to translate or the setting is English-only however well the
 * screens themselves are localised. They are `NSLOCTEXT` rather than markup keys because no document
 * contains them: Unreal's ordinary source gather finds them with no extra step, and they land in the
 * project's own target rather than Folio's, which is where a string with no document belongs.
 *
 * Nothing here is assembled by concatenation. A reader's sentence is a format string with ordered
 * arguments, because "deleted a" and "a deleted" are the same sentence in different languages and a
 * translator has to be able to choose.
 */
namespace Folio::Spoken
{
	/**
	 * What a control's kind is called out loud, or empty for a role that says itself.
	 *
	 * A role word rather than a role name: "check box" is what every screen reader says and what a
	 * player expects to hear, where "checkbox" reads as a tag name to anyone who has used one.
	 */
	FText Role(ESemanticRole Role);

	FText Checked(bool bChecked);

	/** Said last, because it qualifies everything before it. */
	FText Unavailable();
	FText ReadOnly();

	/** Spoken once, when the player turns the reader on and needs to know it took. */
	FText ReaderOn();

	/** A field with nothing in it, which has to be said or an empty box is indistinguishable from a bug. */
	FText Blank();

	FText Deleted(const FText& What);
	FText Selected(const FText& What);

	/** Where a review cursor has run out of screen in either direction. */
	FText TopOfScreen();
	FText EndOfScreen();
	FText NothingOnScreen();

	/**
	 * What one character is called.
	 *
	 * Punctuation typed into a field has to be named - a voice handed a bare full stop says nothing at
	 * all, and a player who cannot see the field has then been told their keystroke did nothing. A
	 * letter or a digit is returned as itself.
	 */
	FText Character(TCHAR Character);

	/** The whole of a typed run, with its punctuation named where a bare character would be silent. */
	FText Typed(const FString& Text);

	/**
	 * Joins the parts of one utterance.
	 *
	 * A comma rather than a full stop, because a reader that pauses like a sentence between a control's
	 * name and its state is slower to listen to and reads as two facts rather than one.
	 */
	FString Join(const TArray<FString>& Parts);

	/** A number as this culture writes it, for a value that came out of a document as digits. */
	FText Number(const FString& Digits);
}

#pragma once

#include "CoreMinimal.h"

namespace Rml
{
	class Element;
}

/**
 * Reading the screen without moving through it.
 *
 * A screen reader is not only a thing that talks when focus moves. Every real one has a second cursor
 * a player drives independently - read the next item, the previous one, that again, all of it - and it
 * exists because focus cannot reach most of a screen: a heading, a paragraph, a warning, a score, the
 * label above the field you are in. A player who can only hear what focus lands on can hear the
 * controls and none of the words around them.
 *
 * A game that self-voices has to provide this itself, because there is no screen reader running to
 * provide it. Bind the four that matter to something a player can reach with one hand.
 *
 * The cursor is over every live document, newest first, so a modal is read before the screen behind
 * it. It follows focus, so arrowing through a menu and then reading on works the way round anyone
 * would expect.
 */
class FFolioReview
{
public:
	enum class EMove : uint8
	{
		Next,
		Previous,
		First,
		Last,
		/**
		 * The next thing that titles a part of the screen.
		 *
		 * How anyone reads a long screen without walking every line of it. It needs the document to
		 * use `<h1>`-`<h6>` or `folio-role="heading"`, which is the same thing that makes it read as
		 * structured to a browser.
		 */
		NextHeading,
		PreviousHeading,
		/** Says the last thing said again, whichever part of the reader said it. */
		Repeat,
	};

	/** Moves and speaks. Returns what was said, empty at either end of the screen. */
	static FString Move(EMove Move);

	/**
	 * Reads from the cursor to the end of the screen, queued rather than interrupting.
	 *
	 * Leaves the cursor where the reading stopped, so a player who silences it partway is left
	 * standing where they got to rather than back at the top.
	 */
	static FString ReadAll();

	/** Drops whatever is queued. What pressing anything during a read-all has to do. */
	static void Stop();

	/** Presses what the cursor is on, without focus having to be there first. */
	static bool Activate();

	/** Puts the document's own focus where the cursor is, which is how a player rejoins the screen. */
	static bool MoveFocusHere();

	/** Keeps the cursor with the focus. Called by the tree when a document reports a focus change. */
	static void FollowFocus(Rml::Element* Element);

	/** Every line the cursor can reach, in order, with the one it is on marked. For the console. */
	static void Describe(TArray<FString>& Out);

	/** The same lines, unmarked and unindented, for anything that is going to read them rather than print them. */
	static void Lines(TArray<FString>& Out);

	/** Whatever holds focus, said the way the reader would say it. Empty when nothing does. */
	static FString Focused();
};

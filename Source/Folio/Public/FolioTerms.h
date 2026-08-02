#pragma once

#include "CoreMinimal.h"

namespace Folio
{
	/**
	 * The names a game shares across its whole script: places, people, items, quests.
	 *
	 * Terms live in `UI/terms/*.terms` and are reached from markup as `{tag:Lookup.Place.X}`.
	 * The files are what the preview tool reads, so a screen resolves its names with no engine
	 * running. See docs/rich_text.md.
	 *
	 * This is the game-facing half. Everything here takes FString and FText and nothing here needs
	 * RmlUi, so a game module can use it without taking a dependency on the renderer's headers.
	 */

	/**
	 * Supplies the real text for a term the tables marked `@external`.
	 *
	 * A game that already holds its names - a data asset, a table of FText - mirrors them into a
	 * `.terms` file so the preview resolves them, and pushes the live text through here. That way
	 * the name is translated once, where it already was, rather than a second time in Folio's own
	 * namespace.
	 *
	 * False when nothing is mirrored under that tag, which is worth acting on: it means the file
	 * and the table it mirrors have drifted apart, and the preview will show a stale name or none.
	 */
	FOLIO_API bool SetTermText(const FString& Tag, const FText& Text);

	/** What a term resolves to right now, or an empty string. */
	FOLIO_API FString GetTermText(const FString& Tag);

	/** Whether a term exists at all, ignoring any `:form` on the tag. */
	FOLIO_API bool HasTerm(const FString& Tag);

	/** Reads `UI/terms` again and rebuilds every open document, as `folio.Terms.Reload` does. */
	FOLIO_API void ReloadTerms();

	/**
	 * Fires after the tables have been read, including on a reload.
	 *
	 * Where a game pushes the text for its `@external` names. Reading the files again drops
	 * whatever was pushed into them, so it has to go back in, and this is the only moment that is
	 * knowable from outside.
	 */
	FOLIO_API FSimpleMulticastDelegate& OnTermsLoaded();
}

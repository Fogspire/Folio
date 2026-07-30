#pragma once

#include "CoreMinimal.h"

namespace Rml
{
	class ElementDocument;
}

/**
 * Resolving a document's user-visible text through Unreal's own localisation.
 *
 * Applied as a pass over the loaded document rather than through
 * `SystemInterface::TranslateString`, and that is not a preference. `TranslateString` receives a
 * string with no element context, so it cannot tell whether the owning element carries a key: it can
 * neither look one up nor notice text that has none. Wiring both would also translate twice.
 *
 * The contract with the markup is `UI_CONVENTIONS.md`'s: every user-visible string sits on an element
 * carrying `data-loc="key"` with the English text inline, so the document stays readable and
 * previewable while the key makes it gatherable. `data-noloc` opts an element out.
 */
namespace Folio::Loc
{
	/**
	 * The gather namespace for a document, derived mechanically from its path.
	 *
	 * `ui/screens/main_menu.rml` gives `UI.MainMenu`; anything under `ui/lib/` gives `UI.Common`,
	 * because a shared component's strings belong to every screen that uses it. Never hand-authored,
	 * so nothing writing markup has to decide it.
	 *
	 * **This has to agree with `Tools/locgather/gather_rml.py` exactly.** The script emits the
	 * NSLOCTEXT declarations the gatherer sees, and this resolves them at runtime; if the two derive
	 * different namespaces, every lookup misses and every string silently falls back to English.
	 */
	FString NamespaceForDocument(const FString& RelativePath);

	/** Replaces the text of every [data-loc] element with its translation, where there is one. */
	void ApplyToDocument(Rml::ElementDocument* Document, const FString& RelativePath);

	/**
	 * Logs every keyed string in a document with what it resolved to.
	 *
	 * How to tell a missing translation from a missing key, which look identical on screen: a key with
	 * no entry in the manifest reports as untranslated, and text with no key does not appear at all.
	 */
	void ReportDocument(Rml::ElementDocument* Document, const FString& RelativePath);
}

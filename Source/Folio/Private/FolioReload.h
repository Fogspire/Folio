#pragma once

#include "CoreMinimal.h"

namespace Rml
{
	class Context;
	class Element;
	class ElementDocument;
}

/**
 * Reloading a document without losing the state a person was looking at.
 *
 * Deliberately a copy of the preview tool's `Reload`/`ElementPath` rather than shared code, and
 * this is not an oversight. RmlUi is a static library linked into both the Folio and FolioCore
 * modules, so each has its own copy of RmlUi's globals: `Rml::Factory::ClearStyleSheetCache` and
 * `Rml::ReleaseTextures` called from a FolioCore translation unit would clear FolioCore's caches,
 * which are empty, and the reload would silently do nothing. Anything touching RmlUi's globals has
 * to live in the module that owns the context.
 */
namespace Folio::Reload
{
	/** What has to survive a document rebuild for the iteration loop to feel continuous. */
	struct FDocumentState
	{
		FString FocusPath;
		/** Element::Focus defaults this off, so restoring without it drops the focus ring. */
		bool bFocusVisible = false;
		TArray<TPair<FString, FVector2f>> Scroll;
	};

	FDocumentState CaptureDocumentState(Rml::Context* Context, Rml::ElementDocument* Document);
	void RestoreDocumentState(Rml::ElementDocument* Document, const FDocumentState& State);

	/**
	 * Reloads stylesheets only, leaving the DOM, focus, scroll and data model untouched.
	 * This is why the granularity split is worth having: a style edit costs nothing.
	 */
	void ReloadStyleSheets(Rml::Context* Context);

	/**
	 * A selector identifying an element well enough to find it again after a rebuild.
	 *
	 * RmlUi gives no stable element identity across a reload, so focus and scroll restoration have
	 * to be expressed as something re-resolvable. An id is used when there is one, since it
	 * survives edits elsewhere in the document; otherwise a structural path.
	 */
	FString ComputeElementPath(Rml::Element* Element);

	/** Resolves a computed path, falling back up it when the exact element is gone. */
	Rml::Element* ResolveElementPath(Rml::ElementDocument* Document, const FString& Path);
}

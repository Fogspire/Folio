#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace uipreview {

class Diagnostics;

/** What has to survive a document rebuild for the iteration loop to feel continuous. */
struct DocumentState {
	std::string focus_path;
	/**
	 * Element::Focus defaults this to false, so restoring without it silently drops the
	 * focus ring and reads as a :focus-visible bug rather than a reload bug.
	 */
	bool focus_visible = false;
	std::vector<std::pair<std::string, std::pair<float, float>>> scroll;
};

DocumentState CaptureDocumentState(Rml::Context* context, Rml::ElementDocument* document);
void RestoreDocumentState(Rml::ElementDocument* document, const DocumentState& state, Diagnostics& diagnostics);

/**
 * Reloads stylesheets only, leaving the DOM, focus, scroll and data model untouched.
 * This is why the reload granularity split is worth having: a style edit costs nothing.
 */
void ReloadStyleSheets(Rml::Context* context);

} // namespace uipreview

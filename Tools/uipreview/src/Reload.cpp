#include "Reload.h"

#include "Diagnostics.h"
#include "ElementPath.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>

namespace uipreview {

DocumentState CaptureDocumentState(Rml::Context* context, Rml::ElementDocument* document)
{
	DocumentState state;
	if (!context || !document)
		return state;

	if (Rml::Element* focused = context->GetFocusElement())
	{
		state.focus_path = ComputeElementPath(focused);
		state.focus_visible = focused->IsPseudoClassSet("focus-visible");
	}

	Rml::ElementList all;
	document->QuerySelectorAll(all, "*");
	for (Rml::Element* element : all)
	{
		const float left = element->GetScrollLeft();
		const float top = element->GetScrollTop();
		if (left != 0.0f || top != 0.0f)
			state.scroll.emplace_back(ComputeElementPath(element), std::make_pair(left, top));
	}

	return state;
}

void RestoreDocumentState(Rml::ElementDocument* document, const DocumentState& state, Diagnostics& diagnostics)
{
	if (!document)
		return;

	if (!state.focus_path.empty())
	{
		if (Rml::Element* element = ResolveElementPath(document, state.focus_path))
		{
			element->Focus(state.focus_visible);
		}
		else
		{
			document->Focus();
			diagnostics.Report(Severity::Warning, "W-RELOAD-FOCUSLOST",
				"could not restore focus to " + state.focus_path + " after reload");
		}
	}

	// Offsets clamp against content size, so this has to follow the layout pass.
	for (const auto& [path, offsets] : state.scroll)
	{
		if (Rml::Element* element = ResolveElementPath(document, path))
		{
			element->SetScrollLeft(offsets.first);
			element->SetScrollTop(offsets.second);
		}
		else
		{
			diagnostics.Report(Severity::Warning, "W-RELOAD-SCROLLLOST",
				"could not restore scroll offset for " + path + " after reload");
		}
	}
}

void ReloadStyleSheets(Rml::Context* context)
{
	if (!context)
		return;

	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();

	for (int i = 0; i < context->GetNumDocuments(); ++i)
	{
		Rml::ElementDocument* document = context->GetDocument(i);
		// The debugger's documents are built from memory and report an empty source URL, so
		// reloading them looks for a file that was never there.
		if (document && !document->GetSourceURL().empty())
			document->ReloadStyleSheet();
	}

	// Spritesheet images may have changed alongside the stylesheet that declares them.
	Rml::ReleaseTextures();
	context->Update();
}

} // namespace uipreview

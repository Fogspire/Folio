#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Types.h>

namespace Rml
{
	class ElementDocument;
}

/**
 * Drag and drop, as far as markup can take it.
 *
 * RmlUi carries a drag from press to release and fires `dragstart`, `dragover`, `dragout`, `dragdrop`
 * and `dragend`, but nothing acts on any of them: a document with `drag: drag-drop` on an icon and a
 * bin to drop it in does nothing at all until something listens. Moving and resizing need no help,
 * because `<handle>` does both natively.
 *
 * So this listens once per document and gives the two things every drop needs. Styling: the dragged
 * element carries `folio-dragging` and a drop zone under the pointer carries `folio-dragover`, which
 * is what makes a bin light up as an icon comes over it. And an action, from `folio-drop` on the zone,
 * for the cases that need no game code at all.
 *
 * Anything beyond that is the game's: a `dragdrop` listener, or `data-event-dragdrop` calling into a
 * bound model. This exists so that the common case needs neither.
 */
class FOLIOCORE_API FFolioDragDrop final : public Rml::EventListener
{
public:
	/** Listens on the document. Safe to call with the same document twice. */
	void Attach(Rml::ElementDocument* InDocument);

	/** Stops listening. Must run before the document is destroyed. */
	void Detach();

	//~ Begin Rml::EventListener
	virtual void ProcessEvent(Rml::Event& Event) override;
	//~ End Rml::EventListener

private:
	Rml::ElementDocument* Document = nullptr;
};

#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <RmlUi/Core/Types.h>

namespace Rml
{
	class ElementDocument;
}

/**
 * The behaviours markup asks for and RmlUi does not supply.
 *
 * RmlUi carries a drag from press to release and fires `dragstart`, `drag`, `dragover`, `dragdrop`
 * and `dragend` - and acts on none of them. `drag: drag` makes an element draggable in the sense that
 * the events happen; nothing moves. So an icon on a desktop and a bin to drop it in do nothing at all
 * until something listens, which is what this is.
 *
 * Moving and resizing a *panel* need none of this: `<handle>` with `move_target` or `size_target` is
 * RmlUi's own and complete.
 *
 * What is here is the small set every UI wants and no game should write twice: drag an element around,
 * drop it somewhere that does something, close a panel and let an animation finish first. Anything
 * past that belongs to the game, through a `dragdrop` listener or `data-event-*` against a model.
 *
 * Lives in FolioCore so the preview tool and the Unreal host install the same one. A behaviour present
 * in only one of them would make the preview lie, which is the one thing the preview cannot do.
 */
class FOLIOCORE_API FFolioBehaviours final : public Rml::EventListener
{
public:
	/**
	 * Detaches, because a listener that outlives its own registration is a use-after-free.
	 *
	 * RmlUi calls OnDetach on every listener an element still holds when it is destroyed, so leaving one
	 * registered past its own lifetime crashes inside the document's destructor rather than here.
	 */
	~FFolioBehaviours();

	/** Listens on the document. Safe to call with the same document twice. */
	void Attach(Rml::ElementDocument* InDocument);

	/** Stops listening. Must run before the document is destroyed. */
	void Detach();

	//~ Begin Rml::EventListener
	virtual void ProcessEvent(Rml::Event& Event) override;
	//~ End Rml::EventListener

private:
	void BeginDrag(Rml::Element& Element, Rml::Event& Event);
	void ContinueDrag(Rml::Event& Event);
	void Drop(Rml::Element& Zone, Rml::Event& Event);
	void Close(Rml::Element& Button);

	/**
	 * Observed rather than held, both of them, because either can be destroyed without telling us.
	 *
	 * A document reload frees every element in it, and `folio-drop="remove"` frees one outright, while a
	 * drag that is interrupted never delivers the `dragend` that would have cleared this. A raw pointer
	 * survives all three and writes into freed memory on the next pointer move. An observer goes null.
	 */
	Rml::ObserverPtr<Rml::Element> Document;

	/** What is being moved, with what it takes to work out where to put it. Null when nothing is. */
	Rml::ObserverPtr<Rml::Element> Moving;
	Rml::Vector2f MoveStart;
	Rml::Vector2f PointerStart;
};

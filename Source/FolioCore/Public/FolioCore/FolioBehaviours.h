#pragma once

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ID.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/ObserverPtr.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/Types.h>

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
 * Both hosts install one, so the preview tool and the game behave the same. A behaviour present in
 * only one of them would make the preview lie, which is the one thing the preview cannot do.
 *
 * Defined in the header rather than compiled into FolioCore, for the same reason the font effect and
 * filter instancers are: RmlUi is a static library linked into both modules, each with its own copy of
 * its globals, and only one of them ran `Rml::Initialise`. `Element::SetProperty` resolves the
 * property through that module's StyleSheetSpecification, so compiled into the other one it reads a
 * vector that was never built and crashes on the first drag.
 */
class FFolioBehaviours final : public Rml::EventListener
{
public:
	/**
	 * Detaches, because a listener that outlives its own registration is a use-after-free.
	 *
	 * RmlUi calls OnDetach on every listener an element still holds when it is destroyed, so leaving one
	 * registered past its own lifetime crashes inside the document's destructor rather than here.
	 */
	~FFolioBehaviours() { Detach(); }

	/** Listens on the document. Safe to call with the same document twice. */
	void Attach(Rml::ElementDocument* InDocument)
	{
		if (Document.get() == InDocument)
		{
			return;
		}

		Detach();

		if (!InDocument)
		{
			return;
		}

		Document = InDocument->GetObserverPtr();

		// Bubbling rather than capture: the target is what each event is about - the dragged element for
		// the drag itself, the zone under the pointer for a drop - and capture would report the document.
		for (const Rml::EventId Id : Events())
		{
			Document->AddEventListener(Id, this);
		}
	}

	/**
	 * Stops listening.
	 *
	 * Safe once the document has been destroyed: it is observed, so this drops a null rather than
	 * unregistering from a freed element.
	 */
	void Detach()
	{
		Moving.reset();

		if (Document)
		{
			for (const Rml::EventId Id : Events())
			{
				Document->RemoveEventListener(Id, this);
			}
		}

		Document.reset();
	}

	//~ Begin Rml::EventListener
	virtual void ProcessEvent(Rml::Event& Event) override
	{
		Rml::Element* Target = Event.GetTargetElement();
		if (!Target)
		{
			return;
		}

		switch (Event.GetId())
		{
		case Rml::EventId::Dragstart:
			BeginDrag(*Target, Event);
			break;

		case Rml::EventId::Drag:
			ContinueDrag(Event);
			break;

		case Rml::EventId::Dragend:
			Target->SetClass(DraggingClass(), false);
			Moving.reset();
			break;

		case Rml::EventId::Dragover:
			// Only a zone that would do something, so an inert element never offers to accept a drop.
			if (Target->HasAttribute("folio-drop"))
			{
				Target->SetClass(DragOverClass(), true);
			}
			break;

		case Rml::EventId::Dragout:
			Target->SetClass(DragOverClass(), false);
			break;

		case Rml::EventId::Dragdrop:
			Drop(*Target, Event);
			break;

		case Rml::EventId::Click:
			if (Target->HasAttribute("folio-close"))
			{
				Close(*Target);
			}
			break;

		case Rml::EventId::Animationend:
			// The end of the way out, whatever the stylesheet made of it.
			if (Target->IsClassSet(ClosingClass()))
			{
				Target->SetProperty("display", "none");
			}
			break;

		default:
			break;
		}
	}
	//~ End Rml::EventListener

private:
	static const Rml::String& DraggingClass()
	{
		static const Rml::String Value = "folio-dragging";
		return Value;
	}

	static const Rml::String& DragOverClass()
	{
		static const Rml::String Value = "folio-dragover";
		return Value;
	}

	static const Rml::String& ClosingClass()
	{
		static const Rml::String Value = "folio-closing";
		return Value;
	}

	static Rml::Vector<Rml::EventId> Events()
	{
		return {
			Rml::EventId::Dragstart,
			Rml::EventId::Drag,
			Rml::EventId::Dragover,
			Rml::EventId::Dragout,
			Rml::EventId::Dragdrop,
			Rml::EventId::Dragend,
			Rml::EventId::Click,
			Rml::EventId::Animationend,
		};
	}

	/** The element being dragged, which a drop event carries as a parameter rather than as its target. */
	static Rml::Element* DraggedElement(Rml::Event& Event)
	{
		return static_cast<Rml::Element*>(Event.GetParameter<void*>("drag_element", nullptr));
	}

	static Rml::Vector2f PointerAt(Rml::Event& Event)
	{
		return Rml::Vector2f(Event.GetParameter<float>("mouse_x", 0.0f),
			Event.GetParameter<float>("mouse_y", 0.0f));
	}

	static void SetPixels(Rml::Element& Element, const Rml::PropertyId Id, const float Value)
	{
		Element.SetProperty(Id, Rml::Property(Value, Rml::Unit::PX));
	}

	/** Whether hiding this element should wait for something to finish first. */
	static bool HasAnimation(Rml::Element& Element)
	{
		const Rml::Property* Animation = Element.GetProperty(Rml::PropertyId::Animation);
		return Animation != nullptr && Animation->unit != Rml::Unit::KEYWORD;
	}

	void BeginDrag(Rml::Element& Element, Rml::Event& Event)
	{
		Element.SetClass(DraggingClass(), true);

		// Cleared whatever happens: a drag that starts on something unmovable must not carry on moving
		// whatever the last one did.
		Moving.reset();

		if (Element.GetAttribute<Rml::String>("folio-drag", "") != "move")
		{
			return;
		}

		// Absolute positioning is what makes this work at all: left and top mean nothing to an element in
		// flow, so a flex item would take the properties and sit exactly where it was.
		const Rml::Style::Position Positioning = Element.GetComputedValues().position();
		if (Positioning != Rml::Style::Position::Absolute && Positioning != Rml::Style::Position::Relative
			&& Positioning != Rml::Style::Position::Fixed)
		{
			Rml::Log::Message(Rml::Log::LT_WARNING,
				"folio-drag=\"move\" on '%s', which is in flow, so setting left and top cannot move it. "
				"Give it position: absolute.", Element.GetAddress().c_str());
			return;
		}

		Moving = Element.GetObserverPtr();
		PointerStart = PointerAt(Event);

		// Taken from the laid-out box rather than from the `left` property, which may be a percentage, a
		// keyword, or absent. Offsets are relative to the offset parent, which is what left and top mean.
		MoveStart = Rml::Vector2f(Element.GetOffsetLeft(), Element.GetOffsetTop());
	}

	void ContinueDrag(Rml::Event& Event)
	{
		if (!Moving)
		{
			return;
		}

		const Rml::Vector2f Delta = PointerAt(Event) - PointerStart;
		SetPixels(*Moving.get(), Rml::PropertyId::Left, MoveStart.x + Delta.x);
		SetPixels(*Moving.get(), Rml::PropertyId::Top, MoveStart.y + Delta.y);
	}

	void Drop(Rml::Element& Zone, Rml::Event& Event)
	{
		Zone.SetClass(DragOverClass(), false);

		const Rml::String Action = Zone.GetAttribute<Rml::String>("folio-drop", "");
		Rml::Element* Dragged = DraggedElement(Event);
		if (Action.empty() || !Dragged)
		{
			return;
		}

		if (Action == "hide")
		{
			Dragged->SetProperty("display", "none");
		}
		else if (Action == "remove")
		{
			if (Rml::Element* Parent = Dragged->GetParentNode())
			{
				if (Moving.get() == Dragged)
				{
					Moving.reset();
				}

				Parent->RemoveChild(Dragged);
			}
		}
		else if (Action != "accept")
		{
			Rml::Log::Message(Rml::Log::LT_WARNING,
				"folio-drop='%s' is not an action. Use hide, remove or accept, or listen for dragdrop "
				"yourself.", Action.c_str());
		}
	}

	void Close(Rml::Element& Button)
	{
		const Rml::String TargetId = Button.GetAttribute<Rml::String>("folio-close", "");
		Rml::Element* Target = TargetId.empty()
			? Button.GetParentNode()
			: (Document ? Document->GetElementById(TargetId) : nullptr);

		if (!Target)
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "folio-close='%s' names no element in this document.",
				TargetId.c_str());
			return;
		}

		// The class first, so a stylesheet gets to animate the way out. Hiding then waits for that to
		// finish; with nothing to wait for, it happens now.
		Target->SetClass(ClosingClass(), true);

		// Adding a class only marks the element's definition dirty - the rules behind it are not resolved
		// until the next update, and reading the animation before then reports whatever was true a moment
		// ago. Which would mean every animated close read as un-animated and vanished instantly.
		if (Rml::ElementDocument* Owner = Target->GetOwnerDocument())
		{
			Owner->UpdateDocument();
		}

		if (!HasAnimation(*Target))
		{
			Target->SetProperty("display", "none");
		}
	}

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

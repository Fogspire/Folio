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

	/**
	 * Says which of the scrolling elements have more above or below what they show.
	 *
	 * `folio-scroll-up` and `folio-scroll-down` go on any element with content past its own box, so a
	 * stylesheet can fade an edge or point at it. RmlUi draws a scrollbar and stops there, and a
	 * scrollbar answers the wrong question: it is a thing to grab with a mouse, it is missing on a
	 * pad entirely, and a list one row too long shows a slider that fills its whole track.
	 *
	 * Called after the context update, when layout has settled. A rule keyed off these classes must
	 * not change the element's own layout, or it changes the answer and the two fight every frame.
	 */
	void UpdateScrollState()
	{
		if (Document)
		{
			StampScroll(Document.get());
		}
	}

	//~ Begin Rml::EventListener
	virtual void ProcessEvent(Rml::Event& Event) override
	{
		// Whatever the last event took out of the tree is freed here, one event later, once RmlUi is no
		// longer walking it. See Discarded.
		Discarded.reset();

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
			if (Rml::Element* Zone = Bearing(Target, "folio-drop"))
			{
				Zone->SetClass(DragOverClass(), true);
			}
			break;

		case Rml::EventId::Dragout:
			if (Rml::Element* Zone = Bearing(Target, "folio-drop"))
			{
				Zone->SetClass(DragOverClass(), false);
			}
			break;

		case Rml::EventId::Dragdrop:
			if (Rml::Element* Zone = Bearing(Target, "folio-drop"))
			{
				Drop(*Zone, Event);
			}
			break;

		case Rml::EventId::Click:
			if (Rml::Element* Button = Bearing(Target, "folio-close"))
			{
				Close(*Button);
			}
			break;

		case Rml::EventId::Animationend:
			Finish(*Target);
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

	/**
	 * The class on the scrolling element, and its counterpart on the parent.
	 *
	 * The parent one is what a fade or an arrow is keyed off. Such an overlay cannot live inside the
	 * scrolling box, or it scrolls away with the content, so it is a sibling - and RCSS matches down
	 * the tree rather than sideways, leaving the parent as the only thing both can see. With two
	 * scrolling children under one parent the last one wins, so give them a parent each.
	 */
	static void Mark(Rml::Element* Element, const char* Edge, const bool bState)
	{
		Element->SetClass(Rml::String("folio-scroll-") + Edge, bState);

		if (Rml::Element* Parent = Element->GetParentNode())
		{
			Parent->SetClass(Rml::String("folio-inner-scroll-") + Edge, bState);
		}
	}

	static void StampScroll(Rml::Element* Element)
	{
		const Rml::ComputedValues& Computed = Element->GetComputedValues();

		if (Computed.overflow_y() != Rml::Style::Overflow::Visible)
		{
			const float Hidden = Element->GetScrollHeight() - Element->GetClientHeight();
			const float At = Element->GetScrollTop();

			// A pixel of slack. A scroll height is a sum of laid out boxes and lands a fraction over
			// its container often enough that an exact test flickers the affordance on and off.
			Mark(Element, "up", Hidden > 1.0f && At > 1.0f);
			Mark(Element, "down", Hidden > 1.0f && At < Hidden - 1.0f);
		}

		if (Computed.overflow_x() != Rml::Style::Overflow::Visible)
		{
			const float Hidden = Element->GetScrollWidth() - Element->GetClientWidth();
			const float At = Element->GetScrollLeft();

			Mark(Element, "left", Hidden > 1.0f && At > 1.0f);
			Mark(Element, "right", Hidden > 1.0f && At < Hidden - 1.0f);
		}

		for (int Index = 0; Index < Element->GetNumChildren(); ++Index)
		{
			StampScroll(Element->GetChild(Index));
		}
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

	/**
	 * The nearest element at or above this one carrying the attribute, or null.
	 *
	 * An event targets the deepest element under the pointer, which for anything with an icon or a label
	 * inside it is a child rather than the thing the author put the attribute on. Dropping on a bin means
	 * dropping on the bin's glyph.
	 */
	static Rml::Element* Bearing(Rml::Element* Element, const char* Attribute)
	{
		for (; Element; Element = Element->GetParentNode())
		{
			if (Element->HasAttribute(Attribute))
			{
				return Element;
			}
		}

		return nullptr;
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

		if (Action == "hide" || Action == "remove")
		{
			if (Moving.get() == Dragged)
			{
				Moving.reset();
			}

			Dismiss(*Dragged, Action);
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

		Dismiss(*Target, "hide");
	}

	/**
	 * Takes an element away, letting a stylesheet animate it out first.
	 *
	 * The class goes on, and what happens at the end is remembered on the element rather than here, so
	 * several can be on their way out at once and a reload cannot leave this holding a stale intent.
	 */
	void Dismiss(Rml::Element& Element, const Rml::String& Action)
	{
		Element.SetAttribute("folio-dismissing", Action);
		Element.SetClass(ClosingClass(), true);

		// Adding a class only marks the element's definition dirty - the rules behind it are not resolved
		// until the next update, and reading the animation before then reports whatever was true a moment
		// ago. Which would mean every animated dismissal read as un-animated and vanished instantly.
		if (Rml::ElementDocument* Owner = Element.GetOwnerDocument())
		{
			Owner->UpdateDocument();
		}

		if (!HasAnimation(Element))
		{
			Finish(Element);
		}
	}

	/** The end of the way out, whatever the stylesheet made of it. */
	void Finish(Rml::Element& Element)
	{
		if (!Element.IsClassSet(ClosingClass()))
		{
			return;
		}

		const Rml::String Action = Element.GetAttribute<Rml::String>("folio-dismissing", "hide");
		Element.RemoveAttribute("folio-dismissing");

		if (Action != "remove")
		{
			Element.SetProperty("display", "none");
			return;
		}

		if (Rml::Element* Parent = Element.GetParentNode())
		{
			if (Moving.get() == &Element)
			{
				Moving.reset();
			}

			// Taken out of the tree now and freed later. RemoveChild hands back the only owning pointer,
			// and dropping it here would destroy an element RmlUi is still dispatching this event on.
			Element.SetProperty("display", "none");
			Discarded = Parent->RemoveChild(&Element);
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

	/**
	 * An element taken out of the tree, held until the next event so it is not freed mid-dispatch.
	 *
	 * A removal always happens while RmlUi is walking that element's own listeners - it is what delivers
	 * the `animationend` the removal waits for - and `RemoveChild` returns the only owning pointer, so
	 * letting it go on the spot is a use-after-free inside the dispatcher.
	 */
	Rml::ElementPtr Discarded;

	/** What is being moved, with what it takes to work out where to put it. Null when nothing is. */
	Rml::ObserverPtr<Rml::Element> Moving;
	Rml::Vector2f MoveStart;
	Rml::Vector2f PointerStart;
};

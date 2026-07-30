#include "FolioCore/FolioBehaviours.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Property.h>

namespace
{
	const Rml::String DraggingClass = "folio-dragging";
	const Rml::String DragOverClass = "folio-dragover";
	const Rml::String ClosingClass = "folio-closing";

	const Rml::EventId Watched[] = {
		Rml::EventId::Dragstart,
		Rml::EventId::Drag,
		Rml::EventId::Dragover,
		Rml::EventId::Dragout,
		Rml::EventId::Dragdrop,
		Rml::EventId::Dragend,
		Rml::EventId::Click,
		Rml::EventId::Animationend,
	};

	/** The element being dragged, which a drop event carries as a parameter rather than as its target. */
	Rml::Element* DraggedElement(Rml::Event& Event)
	{
		return static_cast<Rml::Element*>(Event.GetParameter<void*>("drag_element", nullptr));
	}

	Rml::Vector2f PointerAt(Rml::Event& Event)
	{
		return Rml::Vector2f(Event.GetParameter<float>("mouse_x", 0.0f),
			Event.GetParameter<float>("mouse_y", 0.0f));
	}

	void SetPixels(Rml::Element& Element, const Rml::PropertyId Id, const float Value)
	{
		Element.SetProperty(Id, Rml::Property(Value, Rml::Unit::PX));
	}

	/** Whether hiding this element should wait for something to finish first. */
	bool HasAnimation(Rml::Element& Element)
	{
		const Rml::Property* Animation = Element.GetProperty(Rml::PropertyId::Animation);
		return Animation != nullptr && Animation->unit != Rml::Unit::KEYWORD;
	}
}

void FFolioBehaviours::Attach(Rml::ElementDocument* InDocument)
{
	if (Document == InDocument)
	{
		return;
	}

	Detach();

	Document = InDocument;
	if (!Document)
	{
		return;
	}

	// Bubbling rather than capture: the target is what each event is about - the dragged element for
	// the drag itself, the zone under the pointer for a drop - and capture would report the document.
	for (const Rml::EventId Id : Watched)
	{
		Document->AddEventListener(Id, this);
	}
}

void FFolioBehaviours::Detach()
{
	Moving = nullptr;

	if (!Document)
	{
		return;
	}

	for (const Rml::EventId Id : Watched)
	{
		Document->RemoveEventListener(Id, this);
	}

	Document = nullptr;
}

void FFolioBehaviours::BeginDrag(Rml::Element& Element, Rml::Event& Event)
{
	Element.SetClass(DraggingClass, true);

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

	Moving = &Element;
	PointerStart = PointerAt(Event);

	// Taken from the laid-out box rather than from the `left` property, which may be a percentage, a
	// keyword, or absent. Offsets are relative to the offset parent, which is what left and top mean.
	MoveStart = Rml::Vector2f(Element.GetOffsetLeft(), Element.GetOffsetTop());
}

void FFolioBehaviours::ContinueDrag(Rml::Event& Event)
{
	if (!Moving)
	{
		return;
	}

	const Rml::Vector2f Delta = PointerAt(Event) - PointerStart;
	SetPixels(*Moving, Rml::PropertyId::Left, MoveStart.x + Delta.x);
	SetPixels(*Moving, Rml::PropertyId::Top, MoveStart.y + Delta.y);
}

void FFolioBehaviours::Drop(Rml::Element& Zone, Rml::Event& Event)
{
	Zone.SetClass(DragOverClass, false);

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
			if (Moving == Dragged)
			{
				Moving = nullptr;
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

void FFolioBehaviours::Close(Rml::Element& Button)
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
	Target->SetClass(ClosingClass, true);

	// Adding a class only marks the element's definition dirty - the rules behind it are not resolved
	// until the next update, and reading the animation before then reports whatever was true a moment
	// ago. Which would mean every animated close read as un-animated and vanished instantly.
	if (Document)
	{
		Document->UpdateDocument();
	}

	if (!HasAnimation(*Target))
	{
		Target->SetProperty("display", "none");
	}
}

void FFolioBehaviours::ProcessEvent(Rml::Event& Event)
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
		Target->SetClass(DraggingClass, false);
		Moving = nullptr;
		break;

	case Rml::EventId::Dragover:
		// Only a zone that would do something, so an inert element never offers to accept a drop.
		if (Target->HasAttribute("folio-drop"))
		{
			Target->SetClass(DragOverClass, true);
		}
		break;

	case Rml::EventId::Dragout:
		Target->SetClass(DragOverClass, false);
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
		if (Target->IsClassSet(ClosingClass))
		{
			Target->SetProperty("display", "none");
		}
		break;

	default:
		break;
	}
}

#include "FolioCore/FolioDragDrop.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Log.h>

namespace
{
	const Rml::String DraggingClass = "folio-dragging";
	const Rml::String DragOverClass = "folio-dragover";

	const Rml::EventId Watched[] = {
		Rml::EventId::Dragstart,
		Rml::EventId::Dragover,
		Rml::EventId::Dragout,
		Rml::EventId::Dragdrop,
		Rml::EventId::Dragend,
	};

	/** The element being dragged, which every drop event carries as a parameter rather than as a target. */
	Rml::Element* DraggedElement(Rml::Event& Event)
	{
		return static_cast<Rml::Element*>(Event.GetParameter<void*>("drag_element", nullptr));
	}
}

void FFolioDragDrop::Attach(Rml::ElementDocument* InDocument)
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
	// start and end, the zone under the pointer for the rest - and capture would report the document.
	for (const Rml::EventId Id : Watched)
	{
		Document->AddEventListener(Id, this);
	}
}

void FFolioDragDrop::Detach()
{
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

void FFolioDragDrop::ProcessEvent(Rml::Event& Event)
{
	Rml::Element* Target = Event.GetTargetElement();
	if (!Target)
	{
		return;
	}

	switch (Event.GetId())
	{
	case Rml::EventId::Dragstart:
		Target->SetClass(DraggingClass, true);
		break;

	case Rml::EventId::Dragend:
		Target->SetClass(DraggingClass, false);
		break;

	case Rml::EventId::Dragover:
		// Only a zone that would do something, so an inert element does not offer to accept a drop.
		if (Target->HasAttribute("folio-drop"))
		{
			Target->SetClass(DragOverClass, true);
		}
		break;

	case Rml::EventId::Dragout:
		Target->SetClass(DragOverClass, false);
		break;

	case Rml::EventId::Dragdrop:
	{
		Target->SetClass(DragOverClass, false);

		const Rml::String Action = Target->GetAttribute<Rml::String>("folio-drop", "");
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
			// After the event rather than during it: RmlUi is still walking this element's listeners,
			// and freeing it here pulls the ground out from under that walk.
			if (Rml::Element* Parent = Dragged->GetParentNode())
			{
				Dragged->SetProperty("display", "none");
				Parent->RemoveChild(Dragged);
			}
		}
		else if (Action != "accept")
		{
			Rml::Log::Message(Rml::Log::LT_WARNING,
				"folio-drop='%s' is not an action. Use hide, remove or accept, or listen for dragdrop "
				"yourself.", Action.c_str());
		}
		break;
	}

	default:
		break;
	}
}

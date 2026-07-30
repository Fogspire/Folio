#include "FolioReload.h"

#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "Algo/Reverse.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>

namespace Folio::Reload
{
	FString ComputeElementPath(Rml::Element* Element)
	{
		if (!Element)
		{
			return FString();
		}

		if (const Rml::String& Id = Element->GetId(); !Id.empty())
		{
			return TEXT("#") + Folio::ToFString(Id);
		}

		TArray<FString> Segments;
		for (Rml::Element* Current = Element; Current && Current->GetParentNode(); Current = Current->GetParentNode())
		{
			if (const Rml::String& Id = Current->GetId(); !Id.empty())
			{
				Segments.Add(TEXT("#") + Folio::ToFString(Id));
				break;
			}

			int32 Index = 1;
			Rml::Element* Parent = Current->GetParentNode();
			for (int i = 0; i < Parent->GetNumChildren(); ++i)
			{
				Rml::Element* Sibling = Parent->GetChild(i);
				if (Sibling == Current)
				{
					break;
				}
				if (Sibling->GetTagName() == Current->GetTagName())
				{
					++Index;
				}
			}

			Segments.Add(FString::Printf(TEXT("%s:nth-of-type(%d)"),
				*Folio::ToFString(Current->GetTagName()), Index));
		}

		Algo::Reverse(Segments);
		return FString::Join(Segments, TEXT(" > "));
	}

	Rml::Element* ResolveElementPath(Rml::ElementDocument* Document, const FString& Path)
	{
		if (!Document || Path.IsEmpty())
		{
			return nullptr;
		}

		if (Rml::Element* Found = Document->QuerySelector(Folio::ToRmlString(Path)))
		{
			return Found;
		}

		// Falling back up the path lands somewhere sane when the element's subtree changed.
		FString Shortened = Path;
		for (;;)
		{
			int32 Last = INDEX_NONE;
			if (!Shortened.FindLastChar(TEXT('>'), Last))
			{
				return nullptr;
			}

			Shortened.LeftInline(Last);
			Shortened.TrimEndInline();
			if (Rml::Element* Found = Document->QuerySelector(Folio::ToRmlString(Shortened)))
			{
				return Found;
			}
		}
	}

	FDocumentState CaptureDocumentState(Rml::Context* Context, Rml::ElementDocument* Document)
	{
		FDocumentState State;
		if (!Context || !Document)
		{
			return State;
		}

		if (Rml::Element* Focused = Context->GetFocusElement())
		{
			State.FocusPath = ComputeElementPath(Focused);
			State.bFocusVisible = Focused->IsPseudoClassSet("focus-visible");
		}

		Rml::ElementList All;
		Document->QuerySelectorAll(All, "*");
		for (Rml::Element* Element : All)
		{
			const FVector2f Offsets(Element->GetScrollLeft(), Element->GetScrollTop());
			if (!Offsets.IsNearlyZero())
			{
				State.Scroll.Emplace(ComputeElementPath(Element), Offsets);
			}
		}

		return State;
	}

	void RestoreDocumentState(Rml::ElementDocument* Document, const FDocumentState& State)
	{
		if (!Document)
		{
			return;
		}

		if (!State.FocusPath.IsEmpty())
		{
			if (Rml::Element* Element = ResolveElementPath(Document, State.FocusPath))
			{
				Element->Focus(State.bFocusVisible);
			}
			else
			{
				Document->Focus();
				UE_LOG(LogFolio, Warning, TEXT("could not restore focus to %s after reload"), *State.FocusPath);
			}
		}

		// Offsets clamp against content size, so this has to follow the layout pass.
		for (const TPair<FString, FVector2f>& Entry : State.Scroll)
		{
			if (Rml::Element* Element = ResolveElementPath(Document, Entry.Key))
			{
				Element->SetScrollLeft(Entry.Value.X);
				Element->SetScrollTop(Entry.Value.Y);
			}
		}
	}

	void ReloadStyleSheets(Rml::Context* Context)
	{
		if (!Context)
		{
			return;
		}

		Rml::Factory::ClearStyleSheetCache();
		Rml::Factory::ClearTemplateCache();

		for (int i = 0; i < Context->GetNumDocuments(); ++i)
		{
			Rml::ElementDocument* Document = Context->GetDocument(i);
			// The debugger's documents are built from memory and report an empty source URL, so
			// reloading one looks for a file that was never there.
			if (Document && !Document->GetSourceURL().empty())
			{
				Document->ReloadStyleSheet();
			}
		}

		// Spritesheet images may have changed alongside the stylesheet that declares them.
		Rml::ReleaseTextures();
		Context->Update();
	}
}

#include "FolioFragments.h"

#include "FolioLocalization.h"
#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "FolioCore/FolioFragment.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include <RmlUi/Core/ElementDocument.h>

void FFolioFragments::Add(FFragment Fragment, Rml::ElementDocument* Document)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FolioFragments::Add);

	FFragment& Added = Fragments.Add_GetRef(MoveTemp(Fragment));
	if (!Document)
	{
		return;
	}

	if (!DocumentStyles)
	{
		DocumentStyles = Folio::Fragment::CaptureDocumentStyles(Document);
	}

	Place(Added, Document);
	ApplyStyles(Document);
}

bool FFolioFragments::Remove(const int32 Handle, Rml::ElementDocument* Document, FString& OutOwnModel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FolioFragments::Remove);

	const int32 Index = Fragments.IndexOfByPredicate([Handle](const FFragment& Fragment) { return Fragment.Handle == Handle; });
	if (Index == INDEX_NONE)
	{
		return false;
	}

	FFragment& Fragment = Fragments[Index];
	if (Fragment.Element)
	{
		if (Rml::Element* Slot = Fragment.Element->GetParentNode())
		{
			Slot->RemoveChild(Fragment.Element);
		}
	}

	OutOwnModel = MoveTemp(Fragment.OwnModel);
	Fragments.RemoveAt(Index);

	if (Document && DocumentStyles)
	{
		ApplyStyles(Document);
	}

	return true;
}

void FFolioFragments::Insert(Rml::ElementDocument* Document)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FolioFragments::Insert);

	if (!Document || Fragments.IsEmpty())
	{
		return;
	}

	DocumentStyles = Folio::Fragment::CaptureDocumentStyles(Document);

	for (FFragment& Fragment : Fragments)
	{
		Place(Fragment, Document);
	}

	ApplyStyles(Document);
}

void FFolioFragments::Release()
{
	for (FFragment& Fragment : Fragments)
	{
		Fragment.Element = nullptr;
	}

	DocumentStyles.reset();
}

void FFolioFragments::ReloadStyles(Rml::ElementDocument* Document)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FolioFragments::ReloadStyles);

	if (!Document || Fragments.IsEmpty())
	{
		return;
	}

	DocumentStyles = Folio::Fragment::CaptureDocumentStyles(Document);

	for (FFragment& Fragment : Fragments)
	{
		LoadStyles(Fragment);
	}

	ApplyStyles(Document);
}

void FFolioFragments::Place(FFragment& Fragment, Rml::ElementDocument* Document)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FolioFragments::Place);

	Fragment.Element = nullptr;
	Fragment.Styles.reset();

	Rml::String Markup;
	if (!Folio::Fragment::Read(Folio::ToRmlString(Fragment.Path), Markup))
	{
		UE_LOG(LogFolio, Error, TEXT("fragment '%s' could not be read"), *Fragment.Path);
		return;
	}

	Rml::Element* Slot = Document->GetElementById(Folio::ToRmlString(Fragment.SlotId));
	if (!Slot)
	{
		UE_LOG(LogFolio, Error, TEXT("fragment '%s' names slot '%s', and nothing in '%s' has that id"),
			*Fragment.Path, *Fragment.SlotId, *Folio::ToFString(Document->GetSourceURL()));
		return;
	}

	const Folio::Fragment::FParsed Parsed = Folio::Fragment::Parse(Markup);
	Fragment.Element = Folio::Fragment::Insert(Slot, Parsed);
	Fragment.Styles = Folio::Fragment::LoadStyles(Parsed, Folio::ToRmlString(Fragment.Path));

	Folio::Loc::ApplyToElement(Fragment.Element, Fragment.Path);
}

void FFolioFragments::LoadStyles(FFragment& Fragment)
{
	Rml::String Markup;
	if (Folio::Fragment::Read(Folio::ToRmlString(Fragment.Path), Markup))
	{
		Fragment.Styles = Folio::Fragment::LoadStyles(Folio::Fragment::Parse(Markup), Folio::ToRmlString(Fragment.Path));
	}
}

void FFolioFragments::ApplyStyles(Rml::ElementDocument* Document)
{
	std::vector<const Rml::StyleSheetContainer*> Styles;
	for (const FFragment& Fragment : Fragments)
	{
		Styles.push_back(Fragment.Styles.get());
	}

	Folio::Fragment::ApplyStyles(Document, *DocumentStyles, Styles);
}

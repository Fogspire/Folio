#include "FolioTerms.h"

#include "FolioModule.h"
#include "FolioRmlUi.h"
#include "SFolioCanvas.h"

#include "FolioCore/FolioTerms.h"

namespace Folio
{
	bool SetTermText(const FString& Tag, const FText& Text)
	{
		return FTermTables::Get().SetText(ToRmlString(Tag), ToRmlString(Text.ToString()));
	}

	FString GetTermText(const FString& Tag)
	{
		const FTermEntry* Entry = FTermTables::Get().Find(ToRmlString(Tag));
		return Entry ? ToFString(Entry->Text) : FString();
	}

	bool HasTerm(const FString& Tag)
	{
		return FTermTables::Get().Contains(ToRmlString(Tag));
	}

	FSimpleMulticastDelegate& OnTermsLoaded()
	{
		// Held here rather than on the module, so a game module can subscribe to it without taking
		// the module header - and with it RmlUi - into its own build.
		static FSimpleMulticastDelegate Delegate;
		return Delegate;
	}

	void ReloadTerms()
	{
		if (!FFolioModule::IsAvailable())
		{
			return;
		}

		FFolioModule::Get().LoadTerms();

		// A term is substituted while a document's runs are built, so the tables alone are not
		// enough: every screen already showing the old name has to build them again.
		for (SFolioCanvas* Canvas : TArray<SFolioCanvas*>(SFolioCanvas::GetLiveCanvases()))
		{
			Canvas->ReloadDocument();
		}
	}
}

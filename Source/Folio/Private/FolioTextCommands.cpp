#include "FolioTerms.h"

#include "FolioCore/FolioTerms.h"
#include "FolioCore/FolioTextElement.h"
#include "FolioModule.h"
#include "FolioRmlUi.h"
#include "SFolioCanvas.h"

#include "HAL/IConsoleManager.h"

/**
 * folio.Terms         every loaded term, with what it resolves to
 * folio.Terms.Reload  reads the tables again and rebuilds every open document
 * folio.Text.Skip     completes any text still typing, as the player's skip button does
 */
namespace
{
	void ReportTerms()
	{
		if (!FFolioModule::IsAvailable())
		{
			UE_LOG(LogFolio, Warning, TEXT("the Folio module is not loaded"));
			return;
		}

		Folio::FTermTables& Terms = Folio::FTermTables::Get();

		UE_LOG(LogFolio, Display, TEXT("%s"), *Folio::ToFString(Terms.GetReport()));
		for (const std::string& File : Terms.GetFiles())
		{
			UE_LOG(LogFolio, Display, TEXT("  %s"), *Folio::ToFString(File));
		}

		Terms.ForEach([](const std::string& Key, Folio::FTermEntry& Entry)
		{
			UE_LOG(LogFolio, Display, TEXT("  %-40s %s%s"),
				*Folio::ToFString(Key),
				*Folio::ToFString(Entry.Text),
				Entry.Class.empty() ? TEXT("") : *FString::Printf(TEXT("  [%s]"),
					*Folio::ToFString(Entry.Class)));
		});
	}

	void ReloadTerms()
	{
		if (!FFolioModule::IsAvailable())
		{
			UE_LOG(LogFolio, Warning, TEXT("the Folio module is not loaded"));
			return;
		}

		Folio::ReloadTerms();
	}

	void SkipText()
	{
		int32 Skipped = 0;

		for (SFolioCanvas* Canvas : SFolioCanvas::GetLiveCanvases())
		{
			Rml::ElementDocument* Document = Canvas ? Canvas->GetDocument() : nullptr;
			if (!Document)
			{
				continue;
			}

			Rml::ElementList Texts;
			Document->QuerySelectorAll(Texts, "text");
			for (Rml::Element* Element : Texts)
			{
				if (Folio::Text::FElementRichText* Rich = Folio::Text::AsRichText(Element))
				{
					if (Rich->IsRevealing())
					{
						Rich->Skip();
						++Skipped;
					}
				}
			}
		}

		UE_LOG(LogFolio, Display, TEXT("skipped %d revealing text element(s)"), Skipped);
	}

	FAutoConsoleCommand TermsCommand(
		TEXT("folio.Terms"),
		TEXT("Prints every loaded term, the file it came from and the text it resolves to."),
		FConsoleCommandDelegate::CreateStatic(&ReportTerms));

	FAutoConsoleCommand TermsReloadCommand(
		TEXT("folio.Terms.Reload"),
		TEXT("Reads UI/terms again and rebuilds every open document."),
		FConsoleCommandDelegate::CreateStatic(&ReloadTerms));

	FAutoConsoleCommand SkipCommand(
		TEXT("folio.Text.Skip"),
		TEXT("Completes every <text> still revealing, as a player pressing skip would."),
		FConsoleCommandDelegate::CreateStatic(&SkipText));
}

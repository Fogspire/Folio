#include "FolioLocalization.h"

#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Misc/Paths.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace Folio::Loc
{
	namespace
	{
		/** main_menu -> MainMenu. The one transformation the namespace rule needs. */
		FString ToPascalCase(const FString& Stem)
		{
			FString Out;
			Out.Reserve(Stem.Len());

			bool bUpperNext = true;
			for (const TCHAR Character : Stem)
			{
				if (Character == TEXT('_') || Character == TEXT('-') || Character == TEXT(' '))
				{
					bUpperNext = true;
					continue;
				}

				Out.AppendChar(bUpperNext ? FChar::ToUpper(Character) : Character);
				bUpperNext = false;
			}

			return Out;
		}

		/** Markup, not text: a translation carrying < or & would otherwise be parsed as elements. */
		FString EscapeRml(const FString& Text)
		{
			FString Out = Text;
			Out.ReplaceInline(TEXT("&"), TEXT("&amp;"));
			Out.ReplaceInline(TEXT("<"), TEXT("&lt;"));
			Out.ReplaceInline(TEXT(">"), TEXT("&gt;"));
			return Out;
		}

		/**
		 * True when the element holds text and nothing else.
		 *
		 * Translating replaces the element's entire contents, so a child element inside it would be
		 * swallowed into the string and come back out as escaped, visible markup.
		 */
		bool HoldsOnlyText(Rml::Element* Element)
		{
			for (int i = 0; i < Element->GetNumChildren(); ++i)
			{
				if (Element->GetChild(i)->GetTagName() != "#text")
				{
					return false;
				}
			}
			return true;
		}

		/**
		 * True when a data binding is driving this element's text.
		 *
		 * Its value is game state rather than a translatable string, and rewriting the element's RML
		 * would destroy the view RmlUi built for it at load, leaving the binding permanently dead.
		 * The inline {{ }} form is rewritten into data-text before this runs, on the element or on its
		 * text child depending on the form.
		 */
		bool IsBound(Rml::Element* Element)
		{
			if (Element->HasAttribute("data-text"))
			{
				return true;
			}

			for (int i = 0; i < Element->GetNumChildren(); ++i)
			{
				if (Element->GetChild(i)->HasAttribute("data-text"))
				{
					return true;
				}
			}

			return false;
		}
	}

	FString NamespaceForDocument(const FString& RelativePath)
	{
		FString Path = RelativePath;
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		Path.RemoveFromStart(TEXT("/"));

		// A component's strings belong to every screen that uses it, so they share one namespace
		// rather than being duplicated per screen.
		if (Path.StartsWith(TEXT("UI/lib/"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("/UI/lib/"), ESearchCase::IgnoreCase))
		{
			return TEXT("Folio.Common");
		}

		return TEXT("Folio.") + ToPascalCase(FPaths::GetBaseFilename(Path));
	}

	void ApplyToDocument(Rml::ElementDocument* Document, const FString& RelativePath)
	{
		if (!Document)
		{
			return;
		}

		const FString Namespace = NamespaceForDocument(RelativePath);

		Rml::ElementList Tagged;
		Document->QuerySelectorAll(Tagged, "[data-loc]");

		for (Rml::Element* Element : Tagged)
		{
			const FString Key = Folio::ToFString(Element->GetAttribute<Rml::String>("data-loc", ""));
			if (Key.IsEmpty() || !HoldsOnlyText(Element) || IsBound(Element))
			{
				// Reported by uipreview --strict and by the gather script, which see the markup rather
				// than the built DOM and can point at a line. Repeating it per document load here would
				// be noise on every screen open.
				continue;
			}

			Rml::String Inner;
			Element->GetInnerRML(Inner);

			const FString Source = Folio::ToFString(Inner);
			if (Source.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}

			// AsLocalizable rather than FindTextInLiveTable: it returns the source string when the key
			// is gathered but untranslated, which is every string in the culture it was authored in.
			const FText Localized = FText::AsLocalizable_Advanced(Namespace, Key, Source);
			const FString& Translated = Localized.ToString();

			if (Translated != Source)
			{
				Element->SetInnerRML(Folio::ToRmlString(EscapeRml(Translated)));
			}
		}
	}

	void ReportDocument(Rml::ElementDocument* Document, const FString& RelativePath)
	{
		if (!Document)
		{
			return;
		}

		const FString Namespace = NamespaceForDocument(RelativePath);

		Rml::ElementList Tagged;
		Document->QuerySelectorAll(Tagged, "[data-loc]");

		UE_LOG(LogFolio, Display, TEXT("localisation of '%s' in namespace '%s', culture '%s':"),
			*RelativePath, *Namespace, *FInternationalization::Get().GetCurrentCulture()->GetName());

		int32 Translated = 0;
		for (Rml::Element* Element : Tagged)
		{
			const FString Key = Folio::ToFString(Element->GetAttribute<Rml::String>("data-loc", ""));

			Rml::String Inner;
			Element->GetInnerRML(Inner);
			const FString Shown = Folio::ToFString(Inner);

			// The live table rather than AsLocalizable, because the question here is whether a
			// translation exists at all, not what to display.
			FText Found;
			const bool bHasEntry = FText::FindTextInLiveTable_Advanced(Namespace, Key, Found);
			Translated += bHasEntry ? 1 : 0;

			UE_LOG(LogFolio, Display, TEXT("  %-24s %-8s %s"),
				*Key, bHasEntry ? TEXT("[loc]") : TEXT("[source]"), *Shown);
		}

		UE_LOG(LogFolio, Display, TEXT("  %d keyed string(s), %d with a translation"),
			Tagged.size(), Translated);
	}
}

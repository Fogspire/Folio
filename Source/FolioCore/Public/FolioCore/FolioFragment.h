#pragma once

#include "FolioCore/FolioTrace.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

#include <cctype>
#include <cstring>
#include <vector>

/**
 * Fragments: an .rml file whose body is placed into an element of a document that is already running,
 * and taken out again, while its stylesheets join the document's.
 *
 * Header-only on purpose. Everything here goes through RmlUi's globals (the file interface, the factory,
 * the system interface), and every module that links RmlUi has its own copy of those (see IFolioHost).
 * Inline, the code runs against the copy belonging to whichever module calls it.
 */
namespace Folio::Fragment
{
	/** A fragment file split into the parts that are used separately. */
	struct FParsed
	{
		/** Everything between `<head>` and `</head>`, or empty. */
		Rml::String Head;

		/** The `<body>` tag's attributes as written, carried onto the element the fragment is placed in. */
		Rml::String BodyAttributes;

		/** Everything between `<body>` and `</body>`, or the whole file when it has no body. */
		Rml::String Body;
	};

	namespace Detail
	{
		inline Rml::String StripComments(const Rml::String& Markup)
		{
			Rml::String Out;
			Out.reserve(Markup.size());

			size_t Position = 0;
			while (Position < Markup.size())
			{
				const size_t Open = Markup.find("<!--", Position);
				if (Open == Rml::String::npos)
				{
					Out.append(Markup, Position, Rml::String::npos);
					break;
				}

				Out.append(Markup, Position, Open - Position);
				const size_t Close = Markup.find("-->", Open + 4);
				Position = Close == Rml::String::npos ? Markup.size() : Close + 3;
			}

			return Out;
		}

		/** Where `<Name` starts as a whole tag name, so `<bodyx` is not `<body`. */
		inline size_t FindOpenTag(const Rml::String& Markup, const char* Name, const size_t From = 0)
		{
			const Rml::String Needle = Rml::String("<") + Name;
			for (size_t At = Markup.find(Needle, From); At != Rml::String::npos; At = Markup.find(Needle, At + 1))
			{
				const size_t After = At + Needle.size();
				if (After >= Markup.size())
				{
					return Rml::String::npos;
				}

				const char Next = Markup[After];
				if (Next == '>' || Next == '/' || std::isspace(static_cast<unsigned char>(Next)))
				{
					return At;
				}
			}

			return Rml::String::npos;
		}

		/** One attribute's value from a run of attribute text, or empty. */
		inline Rml::String FindAttribute(const Rml::String& Attributes, const char* Name)
		{
			const size_t NameLength = std::strlen(Name);
			size_t Position = 0;

			while (Position < Attributes.size())
			{
				while (Position < Attributes.size() && std::isspace(static_cast<unsigned char>(Attributes[Position])))
				{
					++Position;
				}

				const size_t KeyStart = Position;
				while (Position < Attributes.size() && Attributes[Position] != '=' && Attributes[Position] != '/'
					&& Attributes[Position] != '>' && !std::isspace(static_cast<unsigned char>(Attributes[Position])))
				{
					++Position;
				}

				const Rml::String Key = Attributes.substr(KeyStart, Position - KeyStart);
				if (Key.empty())
				{
					++Position;
					continue;
				}

				while (Position < Attributes.size() && std::isspace(static_cast<unsigned char>(Attributes[Position])))
				{
					++Position;
				}

				Rml::String Value;
				if (Position < Attributes.size() && Attributes[Position] == '=')
				{
					++Position;
					while (Position < Attributes.size() && std::isspace(static_cast<unsigned char>(Attributes[Position])))
					{
						++Position;
					}

					if (Position < Attributes.size() && (Attributes[Position] == '"' || Attributes[Position] == '\''))
					{
						const char Quote = Attributes[Position++];
						const size_t End = Attributes.find(Quote, Position);
						const size_t Stop = End == Rml::String::npos ? Attributes.size() : End;
						Value = Attributes.substr(Position, Stop - Position);
						Position = Stop + 1;
					}
					else
					{
						const size_t ValueStart = Position;
						while (Position < Attributes.size() && !std::isspace(static_cast<unsigned char>(Attributes[Position]))
							&& Attributes[Position] != '>')
						{
							++Position;
						}
						Value = Attributes.substr(ValueStart, Position - ValueStart);
					}
				}

				if (Key.size() == NameLength && Key == Name)
				{
					return Value;
				}
			}

			return Rml::String();
		}

		/** The text between an element's open tag and its close, with the open tag's attributes. */
		inline bool FindElement(const Rml::String& Markup, const char* Name, Rml::String& OutAttributes, Rml::String& OutInner)
		{
			const size_t Open = FindOpenTag(Markup, Name);
			if (Open == Rml::String::npos)
			{
				return false;
			}

			const size_t OpenEnd = Markup.find('>', Open);
			if (OpenEnd == Rml::String::npos)
			{
				return false;
			}

			const size_t AttributesStart = Open + 1 + std::strlen(Name);
			const bool bSelfClosing = OpenEnd > AttributesStart && Markup[OpenEnd - 1] == '/';
			OutAttributes = Markup.substr(AttributesStart, OpenEnd - AttributesStart - (bSelfClosing ? 1 : 0));

			if (bSelfClosing)
			{
				OutInner.clear();
				return true;
			}

			const Rml::String CloseTag = Rml::String("</") + Name + ">";
			const size_t Close = Markup.rfind(CloseTag);
			const size_t InnerEnd = Close == Rml::String::npos || Close < OpenEnd ? Markup.size() : Close;
			OutInner = Markup.substr(OpenEnd + 1, InnerEnd - OpenEnd - 1);
			return true;
		}
	}

	/**
	 * Splits a fragment file. A full document (`<rml><head>...</head><body>...</body></rml>`) and bare
	 * markup are both accepted, so a fragment can still be opened on its own in a preview.
	 */
	inline FParsed Parse(const Rml::String& Markup)
	{
		FParsed Out;
		const Rml::String Clean = Detail::StripComments(Markup);

		Rml::String HeadAttributes;
		Detail::FindElement(Clean, "head", HeadAttributes, Out.Head);

		if (!Detail::FindElement(Clean, "body", Out.BodyAttributes, Out.Body))
		{
			Out.Body = Clean;
		}

		return Out;
	}

	/** The data model the fragment's body names, or empty when it uses its slot's. */
	inline Rml::String GetModelName(const FParsed& Fragment)
	{
		return Detail::FindAttribute(Fragment.BodyAttributes, "data-model");
	}

	/** Reads through RmlUi's file interface, so the file joins the hot reload dependencies like any other. */
	inline bool Read(const Rml::String& Path, Rml::String& OutMarkup)
	{
		Rml::FileInterface* Files = Rml::GetFileInterface();
		return Files && Files->LoadFile(Path, OutMarkup);
	}

	/**
	 * Builds one container from the fragment head's `<link type="text/rcss">` and `<style>`, in the order
	 * written, with paths resolved against the fragment as a document's are. Null when it has neither.
	 */
	inline Rml::SharedPtr<Rml::StyleSheetContainer> LoadStyles(const FParsed& Fragment, const Rml::String& SourcePath)
	{
		FOLIO_TRACE_SCOPE("Folio/FragmentLoadStyles");

		Rml::SharedPtr<Rml::StyleSheetContainer> Combined;
		const auto Merge = [&Combined](const Rml::StyleSheetContainer& Sheet)
		{
			if (Combined)
			{
				Combined->MergeStyleSheetContainer(Sheet);
			}
			else
			{
				Combined = Sheet.CombineStyleSheetContainer(Rml::StyleSheetContainer());
			}
		};

		const Rml::String& Head = Fragment.Head;
		size_t Position = 0;
		for (;;)
		{
			const size_t Link = Detail::FindOpenTag(Head, "link", Position);
			const size_t Style = Detail::FindOpenTag(Head, "style", Position);
			const size_t Next = Link < Style ? Link : Style;
			if (Next == Rml::String::npos)
			{
				break;
			}

			const size_t TagEnd = Head.find('>', Next);
			if (TagEnd == Rml::String::npos)
			{
				break;
			}

			if (Next == Link)
			{
				Position = TagEnd + 1;

				const Rml::String Attributes = Head.substr(Next + 5, TagEnd - Next - 5);
				const Rml::String Href = Detail::FindAttribute(Attributes, "href");
				if (Detail::FindAttribute(Attributes, "type") != "text/rcss" || Href.empty())
				{
					continue;
				}

				Rml::String Resolved = Href;
				if (Rml::SystemInterface* System = Rml::GetSystemInterface())
				{
					System->JoinPath(Resolved, SourcePath, Href);
				}

				if (const Rml::SharedPtr<Rml::StyleSheetContainer> Sheet = Rml::Factory::InstanceStyleSheetFile(Resolved))
				{
					Merge(*Sheet);
				}
				else
				{
					Rml::Log::Message(Rml::Log::LT_ERROR, "Fragment %s: failed to load style sheet %s.", SourcePath.c_str(), Resolved.c_str());
				}
				continue;
			}

			const size_t Close = Head.find("</style>", TagEnd);
			const size_t ContentEnd = Close == Rml::String::npos ? Head.size() : Close;
			const Rml::String Content = Head.substr(TagEnd + 1, ContentEnd - TagEnd - 1);
			Position = Close == Rml::String::npos ? Head.size() : Close + 8;

			Rml::StreamMemory Stream(reinterpret_cast<const Rml::byte*>(Content.c_str()), Content.size());
			Stream.SetSourceURL(SourcePath);

			Rml::StyleSheetContainer Sheet;
			if (Sheet.LoadStyleSheetContainer(&Stream))
			{
				Merge(Sheet);
			}
		}

		return Combined;
	}

	/**
	 * Appends the fragment to the slot as one `div` wearing the fragment body's attributes, and returns
	 * that element. Parsed in place, so its data views bind to whichever model is in scope there, or to the
	 * one its own `data-model` names, which must already exist.
	 */
	inline Rml::Element* Insert(Rml::Element* Slot, const FParsed& Fragment)
	{
		FOLIO_TRACE_SCOPE("Folio/FragmentInsert");

		if (!Slot)
		{
			return nullptr;
		}

		const int Before = Slot->GetNumChildren();
		Rml::Factory::InstanceElementText(Slot, "<div" + Fragment.BodyAttributes + ">" + Fragment.Body + "</div>");

		return Slot->GetNumChildren() > Before ? Slot->GetChild(Before) : nullptr;
	}

	/** A copy of a document's own stylesheets, kept so fragment styles can be added and taken away again. */
	inline Rml::SharedPtr<Rml::StyleSheetContainer> CaptureDocumentStyles(const Rml::ElementDocument* Document)
	{
		const Rml::StyleSheetContainer* Own = Document ? Document->GetStyleSheetContainer() : nullptr;
		return Own ? Own->CombineStyleSheetContainer(Rml::StyleSheetContainer()) : Rml::MakeShared<Rml::StyleSheetContainer>();
	}

	/** Gives the document its own stylesheets followed by every fragment's, later fragments winning. */
	inline void ApplyStyles(Rml::ElementDocument* Document, const Rml::StyleSheetContainer& DocumentStyles,
		const std::vector<const Rml::StyleSheetContainer*>& FragmentStyles)
	{
		FOLIO_TRACE_SCOPE("Folio/FragmentApplyStyles");

		if (!Document)
		{
			return;
		}

		Rml::SharedPtr<Rml::StyleSheetContainer> Combined = DocumentStyles.CombineStyleSheetContainer(Rml::StyleSheetContainer());
		for (const Rml::StyleSheetContainer* Styles : FragmentStyles)
		{
			if (Styles)
			{
				Combined->MergeStyleSheetContainer(*Styles);
			}
		}

		Document->SetStyleSheetContainer(std::move(Combined));
	}
}

#pragma once

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Elements/ElementProgress.h>
#include <RmlUi/Core/Types.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * What a document means, as opposed to what it looks like: the tree a screen reader is read out of.
 *
 * One definition, two consumers. `uipreview --a11y` checks the annotations against it and can print
 * it with `--a11y-tree`; the engine builds a Slate accessible widget per node from the same walk. Two
 * implementations would drift, and the failure mode is the worst kind - a document that passes the
 * checker and reads as nothing to the person who needs it.
 *
 * Header-only for the reason the rest of FolioCore's RmlUi code is: see FolioBehaviours.h. RmlUi is a
 * static library linked into both modules, each with its own globals, and only one of them ran
 * Rml::Initialise. Nothing here touches those globals - it reads attributes, computed values and the
 * element tree - but compiled into the wrong module it would be one refactor away from doing so.
 */
namespace Folio
{
	/**
	 * What kind of thing a node is. Maps onto the platform's own role list where one exists.
	 *
	 * Two of them have nowhere to go. Unreal's `EAccessibleWidgetType` has no heading and no region,
	 * so those reach a platform screen reader as text and as a pane - the closest honest thing in a
	 * list that cannot be extended from here. They are still worth having: the game's own reader says
	 * the word, and the review cursor can jump between headings, which is how anyone reads a long
	 * screen without walking every line of it.
	 */
	enum class ESemanticRole : unsigned char
	{
		/** Carries nothing a person needs. Its children are still walked. */
		None,
		/** A container worth naming: the document itself, a labelled panel, a dialog. */
		Group,
		Text,
		/** A title over the thing it titles. What structure a screen has is mostly these. */
		Heading,
		/** A named part of a screen a player might want to jump to rather than read past. */
		Region,
		Image,
		Button,
		Link,
		CheckBox,
		Radio,
		ComboBox,
		Slider,
		ProgressBar,
		TextEdit,
		List,
		ListItem,
		/**
		 * A grid of related values, and the two things inside one.
		 *
		 * Unreal has no table type either, so to a platform reader these are panes and text. What they
		 * buy is that the game's own reader can say which column a cell is in, which is the whole
		 * difference between a table and a run of numbers read out in order.
		 */
		Table,
		Row,
		Cell,
	};

	inline const char* RoleName(const ESemanticRole Role)
	{
		switch (Role)
		{
		case ESemanticRole::Group: return "group";
		case ESemanticRole::Text: return "text";
		case ESemanticRole::Heading: return "heading";
		case ESemanticRole::Region: return "region";
		case ESemanticRole::Image: return "image";
		case ESemanticRole::Button: return "button";
		case ESemanticRole::Link: return "link";
		case ESemanticRole::CheckBox: return "checkbox";
		case ESemanticRole::Radio: return "radio";
		case ESemanticRole::ComboBox: return "combobox";
		case ESemanticRole::Slider: return "slider";
		case ESemanticRole::ProgressBar: return "progressbar";
		case ESemanticRole::TextEdit: return "textbox";
		case ESemanticRole::List: return "list";
		case ESemanticRole::ListItem: return "listitem";
		case ESemanticRole::Table: return "table";
		case ESemanticRole::Row: return "row";
		case ESemanticRole::Cell: return "cell";
		default: return "none";
		}
	}

	/**
	 * A control announces itself and nothing inside it.
	 *
	 * A button built out of an icon and a label is one thing to press, not three things to walk past,
	 * and its name already carries whatever text is in it.
	 */
	inline bool RoleIsControl(const ESemanticRole Role)
	{
		switch (Role)
		{
		case ESemanticRole::Button:
		case ESemanticRole::Link:
		case ESemanticRole::CheckBox:
		case ESemanticRole::Radio:
		case ESemanticRole::ComboBox:
		case ESemanticRole::Slider:
		case ESemanticRole::ProgressBar:
		case ESemanticRole::TextEdit:
		case ESemanticRole::Image:
			return true;
		default:
			return false;
		}
	}

	/**
	 * Whether the node's value is the same words as its name.
	 *
	 * True of the two roles that are only text. Reading the value out after the name would say
	 * everything on the screen twice.
	 */
	inline bool RoleSaysItsOwnValue(const ESemanticRole Role)
	{
		return Role == ESemanticRole::Text || Role == ESemanticRole::Heading;
	}

	/** One node of the tree. Geometry is the border box in context units, which is pixels. */
	struct FSemanticNode
	{
		Rml::Element* Element = nullptr;
		ESemanticRole Role = ESemanticRole::None;

		/** What is read out. Empty on a node the author has not named, which `--a11y` reports. */
		std::string Name;
		/** `folio-description`, read after the name where the platform has somewhere to put it. */
		std::string Description;
		/** The current value of a control that has one, already in the form it should be read. */
		std::string Value;

		bool Interactive = false;
		bool Disabled = false;
		/** `folio-announce`: read out again whenever it changes, wherever the reader happens to be. */
		bool Announces = false;
		/** `folio-announce="assertive"`: said now, over whatever is being said. For a warning, not a score. */
		bool AnnouncesAssertively = false;
		bool Checkable = false;
		bool Checked = false;
		bool ReadOnly = true;
		bool Password = false;

		/**
		 * Where this sits among its siblings, one-based, and how many there are. Zero when it is not
		 * one of a set.
		 *
		 * "Sector 2, 2 of 7" rather than "Sector 2". Someone who cannot see the list has no other way
		 * to know how far through it they are, or that there is any more of it.
		 */
		int Position = 0;
		int SetSize = 0;

		/** The column header a cell sits under, said before its value. Empty outside a table. */
		std::string Column;

		/** Set for a slider or a progress bar, which are the two things read as a proportion. */
		bool HasRange = false;
		float Minimum = 0.0f;
		float Maximum = 0.0f;
		float Step = 0.0f;

		Rml::Vector2f Offset;
		Rml::Vector2f Size;

		std::vector<FSemanticNode> Children;
	};

	namespace Detail
	{
		inline std::string Trim(const std::string& Text)
		{
			const auto IsSpace = [](unsigned char c) { return std::isspace(c) != 0; };
			const auto First = std::find_if_not(Text.begin(), Text.end(), IsSpace);
			if (First == Text.end())
			{
				return {};
			}

			const auto Last = std::find_if_not(Text.rbegin(), Text.rend(), IsSpace).base();
			return std::string(First, Last);
		}

		/** Whitespace in markup is layout, not content, so a name never carries a run of it. */
		inline std::string Collapse(const std::string& Text)
		{
			std::string Result;
			Result.reserve(Text.size());

			bool bPendingSpace = false;
			for (const char Character : Text)
			{
				if (std::isspace(static_cast<unsigned char>(Character)))
				{
					bPendingSpace = !Result.empty();
					continue;
				}

				if (bPendingSpace)
				{
					Result += ' ';
					bPendingSpace = false;
				}

				Result += Character;
			}

			return Result;
		}

		/**
		 * The elements RmlUi builds inside its own widgets.
		 *
		 * A scrolling panel grows two scrollbars made of five elements each, none of them authored and
		 * none of them meaningful to walk. The controls that have internals of their own - a slider, a
		 * select - are already opaque because a control announces itself and nothing inside it.
		 */
		inline bool IsWidgetInternal(Rml::Element* Element)
		{
			const Rml::String& Tag = Element->GetTagName();
			return Tag.compare(0, 9, "scrollbar") == 0 || Tag.compare(0, 6, "slider") == 0;
		}

		/**
		 * What one walk needs to know about the whole document, gathered once.
		 *
		 * Naming a control used to ask the document for every `<label>` in it, per element, which made
		 * the walk quadratic on exactly the screens that have the most labels. This is that query,
		 * done once.
		 */
		struct FWalkContext
		{
			/** `<label for="id">` by the id it points at. */
			std::unordered_map<std::string, Rml::Element*> LabelsFor;

			/** Elements already spent naming something else, taken back out of the tree afterwards. */
			std::vector<Rml::Element*> NameSources;
		};
	}

	/**
	 * Whether the element has been put out of reach, as the background behind a modal is.
	 *
	 * Both properties, because either alone is ordinary: `pointer-events: none` is how a decoration
	 * stops eating clicks, and the pair is what `docs/modals.md` tells everyone to write. `folio-inert`
	 * says the same thing outright, for a game that traps focus its own way.
	 *
	 * A screen reader that can still walk into a dialog's background is the same bug as a gamepad that
	 * can: the player is told about controls they cannot reach, with no way to know why nothing
	 * happens.
	 */
	inline bool IsInert(Rml::Element* Element)
	{
		for (Rml::Element* At = Element; At; At = At->GetParentNode())
		{
			if (At->HasAttribute("folio-inert"))
			{
				return true;
			}
		}

		const Rml::ComputedValues& Computed = Element->GetComputedValues();
		return Computed.focus() == Rml::Style::Focus::None
			&& Computed.pointer_events() == Rml::Style::PointerEvents::None;
	}

	/** Decoration the author has taken out of the tree, and everything inside it. */
	inline bool IsHiddenFromAccessibility(Rml::Element* Element)
	{
		return Element && Element->HasAttribute("folio-hidden");
	}

	/** The element's own text, collapsed and trimmed. Empty when it has none directly inside it. */
	inline std::string OwnText(Rml::Element* Element)
	{
		std::string Joined;

		for (int i = 0; i < Element->GetNumChildren(); ++i)
		{
			if (Rml::ElementText* Text = rmlui_dynamic_cast<Rml::ElementText*>(Element->GetChild(i)))
			{
				Joined += Text->GetText();
			}
		}

		return Detail::Trim(Detail::Collapse(Joined));
	}

	/** Every word inside, in order. What names a control whose label sits in a child. */
	inline std::string TextWithin(Rml::Element* Element)
	{
		std::string Joined;

		for (int i = 0; i < Element->GetNumChildren(); ++i)
		{
			Rml::Element* Child = Element->GetChild(i);
			if (IsHiddenFromAccessibility(Child))
			{
				continue;
			}

			if (Rml::ElementText* Text = rmlui_dynamic_cast<Rml::ElementText*>(Child))
			{
				Joined += Text->GetText();
				continue;
			}

			const std::string Inner = TextWithin(Child);
			if (!Inner.empty())
			{
				if (!Joined.empty())
				{
					Joined += ' ';
				}
				Joined += Inner;
			}
		}

		return Detail::Trim(Detail::Collapse(Joined));
	}

	inline bool HasTextWithin(Rml::Element* Element)
	{
		return !TextWithin(Element).empty();
	}

	inline bool IsInteractive(Rml::Element* Element)
	{
		const Rml::String& Tag = Element->GetTagName();
		if (Tag == "button" || Tag == "input" || Tag == "select" || Tag == "textarea" || Tag == "handle")
		{
			return true;
		}

		if (Element->HasAttribute("folio-close") || Element->HasAttribute("folio-drag")
			|| Element->HasAttribute("onclick"))
		{
			return true;
		}

		// tab-index: auto is RCSS's own statement that this takes focus, which is a better signal than
		// any list of tags: it is what the navigation actually walks.
		return Element->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto;
	}

	/**
	 * A control whose caption is written beside it rather than inside it.
	 *
	 * True of every form control and of nothing else. A checkbox has no text of its own and never
	 * will, and a segmented control puts its chip first and its word after - but a `<handle>` with an
	 * empty body is simply unnamed, and letting it take the name of whatever it sits next to would
	 * hide that from the one check that would have caught it.
	 */
	inline bool TakesNameFromBeside(Rml::Element* Element)
	{
		const Rml::String& Tag = Element->GetTagName();
		return Tag == "input" || Tag == "select" || Tag == "textarea";
	}

	/**
	 * What a screen reader reads this control out as.
	 *
	 * `folio-label` wins over text, because a control whose visible label is an icon still needs a
	 * name and because a name that differs from the visible text is occasionally right - "close" on
	 * an X. Everything after it is a place the label is already written and does not want repeating:
	 * text inside, a `<label for>`, or the caption sitting beside a checkbox.
	 */
	inline std::string AccessibleName(Rml::Element* Element, Rml::Element** OutSource = nullptr,
		const Detail::FWalkContext* Context = nullptr)
	{
		const auto From = [OutSource](Rml::Element* Source, std::string Name) {
			if (OutSource)
			{
				*OutSource = Source;
			}
			return Name;
		};

		if (Element->HasAttribute("folio-label"))
		{
			return Detail::Trim(Element->GetAttribute<Rml::String>("folio-label", ""));
		}

		if (std::string Own = OwnText(Element); !Own.empty())
		{
			return Own;
		}

		if (std::string Within = TextWithin(Element); !Within.empty())
		{
			return Within;
		}

		// A checkbox or a radio has no text of its own and never will: the label is the element beside
		// it, which is how the component library is built. Reporting those as unnamed would train
		// people to ignore the check, which is the one failure a linter cannot recover from.
		if (const Rml::String& Id = Element->GetId(); !Id.empty())
		{
			if (Context)
			{
				if (const auto Found = Context->LabelsFor.find(Id); Found != Context->LabelsFor.end())
				{
					if (std::string Text = TextWithin(Found->second); !Text.empty())
					{
						return From(Found->second, Text);
					}
				}
			}
			else if (Rml::ElementDocument* Document = Element->GetOwnerDocument())
			{
				// The one-off path, for a caller asking about a single element rather than walking a
				// document. A walk builds the map instead, because this is a document-wide query.
				Rml::ElementList Labels;
				Document->GetElementsByTagName(Labels, "label");
				for (Rml::Element* Label : Labels)
				{
					if (Label->GetAttribute<Rml::String>("for", "") == Id)
					{
						if (std::string Text = TextWithin(Label); !Text.empty())
						{
							return From(Label, Text);
						}
					}
				}
			}
		}

		// A `<label>` wrapped around the control rather than pointed at it, which is the other half of
		// how the web writes this and reads the same way round.
		if (TakesNameFromBeside(Element))
		{
			for (Rml::Element* At = Element->GetParentNode(); At; At = At->GetParentNode())
			{
				if (At->GetTagName() != "label")
				{
					continue;
				}

				// Not reported as a name source: the label is an ancestor, and taking it out of the
				// tree would take the row it wraps with it. Its text is read twice - once as the
				// control's name and once as itself - which is what a browser does with the same
				// markup, and is the lesser of the two wrongs.
				if (std::string Text = TextWithin(At); !Text.empty())
				{
					return Text;
				}
			}
		}

		// Either side: a segmented control puts its text after the input, a labelled field before it.
		if (TakesNameFromBeside(Element))
		{
			for (Rml::Element* Beside : {Element->GetNextSibling(), Element->GetPreviousSibling()})
			{
				if (Beside && !IsInteractive(Beside))
				{
					if (std::string Text = TextWithin(Beside); !Text.empty())
					{
						return From(Beside, Text);
					}
				}
			}
		}

		return {};
	}

	/**
	 * What kind of control this is.
	 *
	 * `folio-role` first, because a control built out of `div`s is invisible to any amount of tag
	 * inspection, then the tag, which is right whenever the document used the element RmlUi provides.
	 */
	inline ESemanticRole InferRole(Rml::Element* Element)
	{
		if (Element->HasAttribute("folio-role"))
		{
			const Rml::String Role = Element->GetAttribute<Rml::String>("folio-role", "");
			if (Role == "group" || Role == "dialog") return ESemanticRole::Group;
			if (Role == "region" || Role == "landmark") return ESemanticRole::Region;
			if (Role == "heading") return ESemanticRole::Heading;
			if (Role == "text" || Role == "label") return ESemanticRole::Text;
			if (Role == "image" || Role == "img") return ESemanticRole::Image;
			if (Role == "button") return ESemanticRole::Button;
			if (Role == "link") return ESemanticRole::Link;
			if (Role == "checkbox") return ESemanticRole::CheckBox;
			if (Role == "radio") return ESemanticRole::Radio;
			if (Role == "combobox" || Role == "select") return ESemanticRole::ComboBox;
			if (Role == "slider") return ESemanticRole::Slider;
			if (Role == "progressbar") return ESemanticRole::ProgressBar;
			if (Role == "textbox" || Role == "textedit") return ESemanticRole::TextEdit;
			if (Role == "list" || Role == "listbox") return ESemanticRole::List;
			if (Role == "listitem" || Role == "option") return ESemanticRole::ListItem;
			if (Role == "table" || Role == "grid") return ESemanticRole::Table;
			if (Role == "row") return ESemanticRole::Row;
			if (Role == "cell" || Role == "gridcell") return ESemanticRole::Cell;
		}

		const Rml::String& Tag = Element->GetTagName();

		if (Tag == "input")
		{
			const Rml::String Type = Element->GetAttribute<Rml::String>("type", "text");
			if (Type == "checkbox") return ESemanticRole::CheckBox;
			if (Type == "radio") return ESemanticRole::Radio;
			if (Type == "range") return ESemanticRole::Slider;
			if (Type == "submit" || Type == "button") return ESemanticRole::Button;
			return ESemanticRole::TextEdit;
		}

		if (Tag == "textarea") return ESemanticRole::TextEdit;
		if (Tag == "select") return ESemanticRole::ComboBox;
		if (Tag == "option") return ESemanticRole::ListItem;
		if (Tag == "progress") return ESemanticRole::ProgressBar;
		if (Tag == "img") return ESemanticRole::Image;
		if (Tag == "a") return ESemanticRole::Link;
		if (Tag == "button" || Tag == "handle") return ESemanticRole::Button;
		if (Tag == "ul" || Tag == "ol") return ESemanticRole::List;
		if (Tag == "li") return ESemanticRole::ListItem;
		if (Tag == "table") return ESemanticRole::Table;
		if (Tag == "tr") return ESemanticRole::Row;
		if (Tag == "td" || Tag == "th") return ESemanticRole::Cell;

		// The one place a document's structure is already written down. A screen that uses headings
		// the way markup intends gets heading navigation for nothing.
		if (Tag.size() == 2 && Tag[0] == 'h' && Tag[1] >= '1' && Tag[1] <= '6')
		{
			return ESemanticRole::Heading;
		}

		// Anything else that can be pressed is a button, whatever it was built out of. This is what
		// covers the div with an onclick and the one RCSS gave a tab index to.
		if (IsInteractive(Element))
		{
			return ESemanticRole::Button;
		}

		if (!OwnText(Element).empty())
		{
			return ESemanticRole::Text;
		}

		// A container is only worth stopping on if the author named it. Everything else is layout, and
		// a tree full of unnamed panes is how a screen reader becomes something people switch off.
		if (Element->HasAttribute("folio-label"))
		{
			return ESemanticRole::Group;
		}

		return ESemanticRole::None;
	}

	/** Whether the element occupies space and has not been hidden outright. */
	inline bool IsLaidOut(Rml::Element* Element)
	{
		const Rml::Vector2f Size = Element->GetBox().GetSize(Rml::BoxArea::Border);

		return Size.x > 0.0f && Size.y > 0.0f
			&& Element->GetComputedValues().visibility() == Rml::Style::Visibility::Visible;
	}

	/** Whether the element is on screen at all. Faded out is not on screen. */
	inline bool IsVisibleForAccessibility(Rml::Element* Element)
	{
		return IsLaidOut(Element) && Element->GetComputedValues().opacity() > 0.0f;
	}

	/**
	 * Whether the tree keeps this element and what is inside it.
	 *
	 * Faded out means gone, with one exception that is the whole reason this is not simply the
	 * visibility test: a control at zero opacity over its own hit area is how every styled checkbox,
	 * radio and segmented control in the library is built - the input is invisible and the thing you
	 * see is a sibling. It is still what focus lands on and still what a click hits, so it is still
	 * what a screen reader has to be able to reach. RmlUi multiplies opacity down the tree, so a
	 * panel faded to nothing takes its controls with it and only the deliberate case survives.
	 */
	inline bool IsInAccessibilityTree(Rml::Element* Element)
	{
		if (!IsLaidOut(Element))
		{
			return false;
		}

		return Element->GetComputedValues().opacity() > 0.0f || IsInteractive(Element);
	}

	/** The attribute or the pseudo class, and an ancestor's disabled attribute disables the lot. */
	inline bool IsDisabled(Rml::Element* Element)
	{
		for (Rml::Element* At = Element; At; At = At->GetParentNode())
		{
			if (At->HasAttribute("disabled") || At->IsPseudoClassSet("disabled"))
			{
				return true;
			}
		}

		return false;
	}

	namespace Detail
	{
		inline float AttributeAsFloat(Rml::Element* Element, const char* Name, const float Fallback)
		{
			const Rml::String Value = Element->GetAttribute<Rml::String>(Name, "");
			if (Value.empty())
			{
				return Fallback;
			}

			try
			{
				return std::stof(Value);
			}
			catch (...)
			{
				return Fallback;
			}
		}

		/** Trailing zeros in a number nobody typed are three syllables of nothing. */
		inline std::string FormatNumber(const float Value)
		{
			std::string Text = std::to_string(Value);
			if (Text.find('.') == std::string::npos)
			{
				return Text;
			}

			Text.erase(Text.find_last_not_of('0') + 1);
			if (!Text.empty() && Text.back() == '.')
			{
				Text.pop_back();
			}

			return Text;
		}

		inline void ReadState(Rml::Element* Element, FSemanticNode& Node)
		{
			Node.Disabled = IsDisabled(Element);

			switch (Node.Role)
			{
			case ESemanticRole::CheckBox:
			case ESemanticRole::Radio:
				Node.Checkable = true;

				// `folio-checked` is for a control built out of divs, where there is no input to carry
				// the state and a class name is the only thing that has it. Bind it with
				// data-attr-folio-checked; anything but "false" or "0" is on.
				if (Element->HasAttribute("folio-checked"))
				{
					const Rml::String Value = Element->GetAttribute<Rml::String>("folio-checked", "");
					Node.Checked = Value != "false" && Value != "0";
				}
				else
				{
					Node.Checked = Element->HasAttribute("checked") || Element->IsPseudoClassSet("checked");
				}

				Node.ReadOnly = false;
				break;

			case ESemanticRole::Slider:
				Node.HasRange = true;
				Node.ReadOnly = Element->HasAttribute("readonly");
				Node.Minimum = AttributeAsFloat(Element, "min", 0.0f);
				Node.Maximum = AttributeAsFloat(Element, "max", 100.0f);
				Node.Step = AttributeAsFloat(Element, "step", 1.0f);
				if (Rml::ElementFormControl* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
				{
					// A range reports its position as a float, and "sixty point zero zero zero zero
					// zero zero" is not what anyone wants read out.
					try
					{
						Node.Value = FormatNumber(std::stof(Control->GetValue()));
					}
					catch (...)
					{
						Node.Value = Control->GetValue();
					}
				}
				break;

			case ESemanticRole::ProgressBar:
				Node.HasRange = true;
				if (Rml::ElementProgress* Progress = rmlui_dynamic_cast<Rml::ElementProgress*>(Element))
				{
					Node.Maximum = Progress->GetMax();
					Node.Value = FormatNumber(Progress->GetValue());
				}
				break;

			case ESemanticRole::TextEdit:
			{
				const Rml::String Type = Element->GetAttribute<Rml::String>("type", "text");
				Node.Password = Type == "password";
				Node.ReadOnly = Element->HasAttribute("readonly");
				if (Rml::ElementFormControl* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
				{
					// Never the characters themselves. A screen reader reading a password aloud is the
					// one place where doing the accessible thing is the wrong thing.
					Node.Value = Node.Password ? std::string() : std::string(Control->GetValue());
				}
				break;
			}

			case ESemanticRole::ComboBox:
				Node.ReadOnly = false;
				if (Rml::ElementFormControlSelect* Select =
					rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(Element))
				{
					// The option's text rather than its value: one is written for a person, the other
					// for whatever reads the form.
					if (Rml::Element* Option = Select->GetOption(Select->GetSelection()))
					{
						Node.Value = TextWithin(Option);
					}

					if (Node.Value.empty())
					{
						Node.Value = Select->GetValue();
					}
				}
				break;

			case ESemanticRole::Text:
			case ESemanticRole::Heading:
				Node.Value = Node.Name;
				break;

			default:
				break;
			}
		}

		/**
		 * The name a node of this role would carry.
		 *
		 * A group is named by its label and nothing else. Falling back to the text inside one would
		 * name a panel with the whole screen.
		 */
		inline std::string NodeName(Rml::Element* Element, const ESemanticRole Role,
			Rml::Element** OutSource = nullptr, const FWalkContext* Context = nullptr)
		{
			// Containers are named by their label or not at all. Falling back to the text inside one
			// would name a row with every cell in it, and a panel with the whole screen.
			if (Role == ESemanticRole::Group || Role == ESemanticRole::Region
				|| Role == ESemanticRole::List || Role == ESemanticRole::Table
				|| Role == ESemanticRole::Row)
			{
				return Trim(Element->GetAttribute<Rml::String>("folio-label", ""));
			}

			return AccessibleName(Element, OutSource, Context);
		}

		inline FSemanticNode MakeNode(Rml::Element* Element, const ESemanticRole Role,
			FWalkContext* Context = nullptr)
		{
			FSemanticNode Node;
			Node.Element = Element;
			Node.Role = Role;
			Node.Interactive = IsInteractive(Element);
			Node.Announces = Element->HasAttribute("folio-announce");
			Node.AnnouncesAssertively = Node.Announces
				&& Element->GetAttribute<Rml::String>("folio-announce", "") == "assertive";
			Node.Offset = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
			Node.Size = Element->GetBox().GetSize(Rml::BoxArea::Border);
			Node.Description = Trim(Element->GetAttribute<Rml::String>("folio-description", ""));

			Rml::Element* Source = nullptr;
			Node.Name = NodeName(Element, Role, &Source, Context);

			// The caption a control borrowed its name from is not read again as text of its own, or a
			// checkbox and its label come out as "Subtitles, check box, checked. Subtitles."
			if (Source && Context)
			{
				Context->NameSources.push_back(Source);
			}

			ReadState(Element, Node);
			return Node;
		}

		/** Every `<label for>` in the document, so naming a control is a lookup rather than a query. */
		inline void CollectLabels(Rml::Element* Element, FWalkContext& Context)
		{
			if (Element->GetTagName() == "label")
			{
				if (const Rml::String For = Element->GetAttribute<Rml::String>("for", ""); !For.empty())
				{
					Context.LabelsFor.emplace(For, Element);
				}
			}

			for (int i = 0; i < Element->GetNumChildren(); ++i)
			{
				CollectLabels(Element->GetChild(i), Context);
			}
		}

		/**
		 * Numbers the children of a set, so an item can say how far through it is.
		 *
		 * Counted after the walk rather than from the markup, because what a player is moving through
		 * is what survived the walk: an item that is hidden, faded out or inert is not one of seven.
		 */
		inline void NumberChildren(FSemanticNode& Node, const ESemanticRole Of)
		{
			int Count = 0;
			for (const FSemanticNode& Child : Node.Children)
			{
				Count += Child.Role == Of ? 1 : 0;
			}

			if (Count < 2)
			{
				// One of one is not a position, it is a sentence nobody needed.
				return;
			}

			int At = 0;
			for (FSemanticNode& Child : Node.Children)
			{
				if (Child.Role == Of)
				{
					Child.Position = ++At;
					Child.SetSize = Count;
				}
			}
		}

		/**
		 * Gives every cell the name of the column it is under.
		 *
		 * A table read without them is a run of numbers in order, which is exactly as useful as it
		 * sounds. The headers come from the first row that is made of `<th>`, which is where markup
		 * already puts them.
		 */
		inline void LabelTableColumns(FSemanticNode& Table)
		{
			const FSemanticNode* Header = nullptr;
			for (const FSemanticNode& Row : Table.Children)
			{
				if (Row.Role != ESemanticRole::Row)
				{
					continue;
				}

				const bool bAllHeaders = !Row.Children.empty()
					&& std::all_of(Row.Children.begin(), Row.Children.end(),
						[](const FSemanticNode& Cell)
						{
							return Cell.Element && Cell.Element->GetTagName() == "th";
						});

				if (bAllHeaders)
				{
					Header = &Row;
				}

				break;
			}

			if (!Header)
			{
				return;
			}

			std::vector<std::string> Columns;
			Columns.reserve(Header->Children.size());
			for (const FSemanticNode& Cell : Header->Children)
			{
				Columns.push_back(Cell.Name);
			}

			for (FSemanticNode& Row : Table.Children)
			{
				if (Row.Role != ESemanticRole::Row || &Row == Header)
				{
					continue;
				}

				for (std::size_t Index = 0; Index < Row.Children.size() && Index < Columns.size(); ++Index)
				{
					Row.Children[Index].Column = Columns[Index];
				}
			}
		}

		/**
		 * One pass over the document, producing nodes and noting what named what.
		 *
		 * A caption is as often before the control it names as after it, so the elements to remove are
		 * only fully known once the walk is over - which is why they are pruned afterwards rather than
		 * skipped on the way. It used to be two walks and a document-wide label query per element.
		 */
		inline void Walk(Rml::Element* Element, FWalkContext& Context, std::vector<FSemanticNode>& Out)
		{
			if (!Element || IsHiddenFromAccessibility(Element) || IsWidgetInternal(Element))
			{
				return;
			}

			if (!IsInAccessibilityTree(Element) || IsInert(Element))
			{
				return;
			}

			const ESemanticRole Role = InferRole(Element);
			if (Role == ESemanticRole::None)
			{
				for (int i = 0; i < Element->GetNumChildren(); ++i)
				{
					Walk(Element->GetChild(i), Context, Out);
				}
				return;
			}

			FSemanticNode Node = MakeNode(Element, Role, &Context);

			if (!RoleIsControl(Role))
			{
				for (int i = 0; i < Element->GetNumChildren(); ++i)
				{
					Walk(Element->GetChild(i), Context, Node.Children);
				}

				switch (Role)
				{
				case ESemanticRole::List: NumberChildren(Node, ESemanticRole::ListItem); break;
				case ESemanticRole::Row: NumberChildren(Node, ESemanticRole::Cell); break;
				case ESemanticRole::Table:
					NumberChildren(Node, ESemanticRole::Row);
					LabelTableColumns(Node);
					break;
				default: break;
				}
			}

			Out.push_back(std::move(Node));
		}

		/** Takes back out the nodes whose element was spent naming something else. */
		inline void PruneNameSources(std::vector<FSemanticNode>& Nodes,
			const std::vector<Rml::Element*>& Sources)
		{
			const auto Spent = [&Sources](const FSemanticNode& Node) {
				return std::find(Sources.begin(), Sources.end(), Node.Element) != Sources.end();
			};

			Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(), Spent), Nodes.end());

			for (FSemanticNode& Node : Nodes)
			{
				PruneNameSources(Node.Children, Sources);
			}
		}
	}

	/**
	 * The document as a tree of things a person can be told about.
	 *
	 * The root is the document itself, named by its `<title>`, so a screen reader announces which
	 * screen just opened rather than starting straight into its first button.
	 */
	inline FSemanticNode BuildSemanticTree(Rml::ElementDocument* Document)
	{
		FSemanticNode Root;
		if (!Document)
		{
			return Root;
		}

		Root.Element = Document;
		Root.Role = ESemanticRole::Group;
		Root.Name = Detail::Trim(Document->GetTitle());
		Root.Offset = Document->GetAbsoluteOffset(Rml::BoxArea::Border);
		Root.Size = Document->GetBox().GetSize(Rml::BoxArea::Border);

		Detail::FWalkContext Context;
		Detail::CollectLabels(Document, Context);

		for (int i = 0; i < Document->GetNumChildren(); ++i)
		{
			Detail::Walk(Document->GetChild(i), Context, Root.Children);
		}

		if (!Context.NameSources.empty())
		{
			Detail::PruneNameSources(Root.Children, Context.NameSources);
		}

		return Root;
	}

	/** Depth first, in reading order, which is also the order a screen reader walks. */
	template <typename Visitor>
	void VisitSemanticTree(const FSemanticNode& Node, Visitor&& Visit, const int Depth = 0)
	{
		Visit(Node, Depth);
		for (const FSemanticNode& Child : Node.Children)
		{
			VisitSemanticTree(Child, Visit, Depth + 1);
		}
	}
}

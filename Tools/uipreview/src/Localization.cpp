#include "Localization.h"

#include "Paths.h"

#include "Diagnostics.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

namespace uipreview {

namespace {

/** Latin-1 and Latin Extended-A stand-ins, chosen to stay recognisable when read. */
struct Accent {
	char plain;
	const char* accented;
};

const Accent kAccents[] = {
	{'a', "\xC3\xA1"}, {'b', "\xC9\x93"}, {'c', "\xC3\xA7"}, {'d', "\xC4\x91"}, {'e', "\xC3\xA9"},
	{'f', "\xC6\x92"}, {'g', "\xC4\x9F"}, {'h', "\xC4\xA5"}, {'i', "\xC3\xAD"}, {'j', "\xC4\xB5"},
	{'k', "\xC4\xB7"}, {'l', "\xC4\xBA"}, {'n', "\xC3\xB1"}, {'o', "\xC3\xB3"}, {'p', "\xC3\xBE"},
	{'r', "\xC5\x99"}, {'s', "\xC5\xA1"}, {'t', "\xC5\xA5"}, {'u', "\xC3\xBA"}, {'w', "\xC5\xB5"},
	{'y', "\xC3\xBD"}, {'z', "\xC5\xBE"},
	{'A', "\xC3\x81"}, {'C', "\xC3\x87"}, {'D', "\xC4\x90"}, {'E', "\xC3\x89"}, {'G', "\xC4\x9E"},
	{'I', "\xC3\x8D"}, {'L', "\xC4\xB9"}, {'N', "\xC3\x91"}, {'O', "\xC3\x93"}, {'R', "\xC5\x98"},
	{'S', "\xC5\xA0"}, {'T', "\xC5\xA4"}, {'U', "\xC3\x9A"}, {'Y', "\xC3\x9D"}, {'Z', "\xC5\xBD"},
};

const char* kFiller[] = {"\xC3\xA9\xC5\xBEt\xC5\x99\xC3\xA1", "\xC5\x82\xC3\xB3\xC3\xB1\xC7\xA5\xC3\xA9\xC5\x99",
	"t\xC3\xA9\xC5\xA1t", "\xC5\xA1t\xC5\x99\xC3\xADn\xC7\xA5"};

/** Counts code points rather than bytes, so inflation is measured the way a reader sees it. */
size_t CountCodePoints(const std::string& text)
{
	size_t count = 0;
	for (const unsigned char c : text)
	{
		if ((c & 0xC0) != 0x80)
			++count;
	}
	return count;
}

std::string EscapeRml(const std::string& text)
{
	std::string out;
	for (const char c : text)
	{
		switch (c)
		{
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '&': out += "&amp;"; break;
		default: out += c; break;
		}
	}
	return out;
}

bool IsWhitespaceOnly(const std::string& text)
{
	return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

} // namespace

bool Localizer::Load(const std::string& root, const std::string& culture)
{
	if (culture.empty())
		return true;

	active = true;

	if (culture == "pseudo")
	{
		pseudo = true;
		return true;
	}

	path = UiDirectory(root) + "/loc/" + culture + ".json";
	std::ifstream file(path);
	if (!file)
	{
		// An explicitly requested culture that does not exist is a mistake worth failing on,
		// not something to silently fall back from.
		diagnostics.Report(Severity::Error, "E-LOC-NOFILE", "no translations at " + diagnostics.Relative(path));
		return false;
	}

	try
	{
		nlohmann::json parsed;
		file >> parsed;
		for (auto& entry : parsed.items())
		{
			if (entry.value().is_string())
				strings[entry.key()] = entry.value().get<std::string>();
		}
	}
	catch (const std::exception& e)
	{
		diagnostics.Report(Severity::Error, "E-LOC-PARSE", std::string("could not parse ") + path + ": " + e.what());
		return false;
	}

	return true;
}

std::optional<std::string> Localizer::Lookup(const std::string& key) const
{
	const auto it = strings.find(key);
	if (it == strings.end())
		return std::nullopt;
	return it->second;
}

std::string Localizer::Pseudoize(const std::string& text) const
{
	if (text.empty())
		return text;

	std::string body;
	body.reserve(text.size() * 2);

	for (size_t i = 0; i < text.size();)
	{
		// Data expressions must survive verbatim or the binding breaks.
		if (text.compare(i, 2, "{{") == 0)
		{
			const size_t end = text.find("}}", i);
			const size_t stop = (end == std::string::npos) ? text.size() : end + 2;
			body.append(text, i, stop - i);
			i = stop;
			continue;
		}

		// printf-style and brace placeholders likewise.
		if (text[i] == '%' && i + 1 < text.size())
		{
			size_t stop = i + 1;
			while (stop < text.size() && (std::isdigit((unsigned char)text[stop]) || text[stop] == '$'))
				++stop;
			if (stop < text.size())
				++stop;
			body.append(text, i, stop - i);
			i = stop;
			continue;
		}
		if (text[i] == '{')
		{
			const size_t end = text.find('}', i);
			if (end != std::string::npos && end - i <= 8)
			{
				body.append(text, i, end - i + 1);
				i = end + 1;
				continue;
			}
		}

		bool replaced = false;
		for (const Accent& accent : kAccents)
		{
			if (text[i] == accent.plain)
			{
				body += accent.accented;
				replaced = true;
				break;
			}
		}
		if (!replaced)
			body += text[i];
		++i;
	}

	// Inflate with whole words, which preserves realistic wrap points. Character doubling
	// would not, and wrapping is what this mode exists to test.
	const size_t original = CountCodePoints(text);
	const size_t target = (size_t)std::ceil((double)original * 1.4);
	size_t current = CountCodePoints(body);

	size_t index = 0;
	while (current < target)
	{
		const char* word = kFiller[index % (sizeof kFiller / sizeof kFiller[0])];
		++index;

		const size_t word_length = CountCodePoints(word);
		if (current + 1 + word_length > target && current + 1 < target)
		{
			// Trim the last word so the result lands on the target length exactly.
			const size_t room = target - current - 1;
			std::string trimmed;
			size_t taken = 0;
			for (size_t i = 0; word[i] && taken < room; ++i)
			{
				trimmed += word[i];
				if ((((unsigned char)word[i + 1]) & 0xC0) != 0x80)
					++taken;
			}
			body += " " + trimmed;
			break;
		}

		body += " ";
		body += word;
		current += 1 + word_length;
	}

	// ASCII brackets specifically: they exist in every font, and a missing closing bracket is
	// the clearest possible signal that a string is being clipped.
	return "[" + body + "]";
}

void Localizer::ApplyToDocument(Rml::ElementDocument* document, bool lint_untagged)
{
	if (!document || !active)
		return;

	Rml::ElementList tagged;
	document->QuerySelectorAll(tagged, "[data-loc]");

	for (Rml::Element* element : tagged)
	{
		const Rml::String key = element->GetAttribute<Rml::String>("data-loc", "");
		Rml::String source;
		element->GetInnerRML(source);

		if (key.empty() || IsWhitespaceOnly(source))
		{
			diagnostics.Report(Severity::Warning, "W-LOC-EMPTY",
				"element with data-loc=\"" + key + "\" has no text content");
			continue;
		}

		// Translating replaces the element's entire contents, so a child element inside it
		// is swallowed into the string and comes back out as escaped, visible markup. The
		// key has to sit on an element that holds text and nothing else.
		bool has_element_child = false;
		for (int i = 0; i < element->GetNumChildren(); ++i)
			has_element_child |= element->GetChild(i)->GetTagName() != "#text";

		if (has_element_child)
		{
			diagnostics.Report(Severity::Warning, "W-LOC-MARKUP",
				"element with data-loc=\"" + key + "\" contains child elements; translation "
				"replaces all of its content, so the key belongs on an inner text-only element",
				{}, 0, {{"key", key}});
			continue;
		}

		std::string text;
		if (const std::optional<std::string> translated = Lookup(key))
		{
			text = *translated;
		}
		else
		{
			text = source;
			if (!pseudo)
			{
				diagnostics.Report(Severity::Warning, "W-LOC-MISSING",
					"no translation for '" + key + "'; showing the source text");
			}
		}

		if (pseudo)
			text = Pseudoize(text);

		element->SetInnerRML(EscapeRml(text));
	}

	if (!lint_untagged)
		return;

	// Literal text with no key cannot be translated at all, which is the same class of
	// silent failure as art that is referenced but never declared.
	Rml::ElementList all;
	document->QuerySelectorAll(all, "*");
	for (Rml::Element* element : all)
	{
		if (element->HasAttribute("data-loc") || element->HasAttribute("data-noloc"))
			continue;

		// Text is itself a child element, so "has no children" would exempt every element
		// that has any text in it. What matters is that the element holds text and nothing
		// but text, so that the text found here is its own rather than a descendant's.
		bool holds_only_text = false;
		for (int i = 0; i < element->GetNumChildren(); ++i)
		{
			if (element->GetChild(i)->GetTagName() != "#text")
			{
				holds_only_text = false;
				break;
			}
			holds_only_text = true;
		}

		if (!holds_only_text)
			continue;

		// A value arriving through a data binding is game state, not a translatable string.
		// The inline {{ }} form is rewritten into data-text before this runs, so the
		// attribute is what to look for; the raw form is still checked for the cases where
		// the substitution has not happened yet.
		if (element->HasAttribute("data-text"))
			continue;

		bool bound = false;
		for (int i = 0; i < element->GetNumChildren() && !bound; ++i)
			bound = element->GetChild(i)->HasAttribute("data-text");
		if (bound)
			continue;

		Rml::String text;
		element->GetInnerRML(text);
		if (IsWhitespaceOnly(text) || text.find("{{") != std::string::npos)
			continue;

		diagnostics.Report(Severity::Warning, "W-LOC-UNTAGGED",
			"text without a data-loc key cannot be translated: \"" + text + "\"",
			{}, 0, {{"text", text}});
	}
}

} // namespace uipreview

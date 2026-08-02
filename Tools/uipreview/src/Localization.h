#pragma once

#include <map>
#include <optional>
#include <string>

namespace Rml {
class ElementDocument;
}

namespace uipreview {

class Diagnostics;

/**
 * Resolves user-visible text through `data-loc` keys, and can inflate it to expose layouts
 * that only fit English.
 *
 * Localization is applied as a pass over the document rather than through
 * `SystemInterface::TranslateString`, because that hook receives text with no element
 * context: it cannot tell whether the owning element carries a key, so it can neither look
 * one up nor detect untagged text. The two mechanisms would also double-apply.
 */
class Localizer {
public:
	explicit Localizer(Diagnostics& diagnostics) : diagnostics(diagnostics) {}

	/** `pseudo` needs no file; any other code reads <root>/UI/loc/<code>.json. */
	bool Load(const std::string& root, const std::string& culture);

	bool IsActive() const { return active; }
	bool IsPseudo() const { return pseudo; }
	const std::string& Path() const { return path; }

	/** Replaces the text of every [data-loc] element, and optionally lints untagged text. */
	void ApplyToDocument(Rml::ElementDocument* document, bool lint_untagged);

	/**
	 * Accents the letters, lengthens the string by about 40% and wraps it in brackets.
	 * Placeholders and data expressions are passed through untouched, since corrupting them
	 * manufactures bugs that do not exist. The brackets are the useful part: if the closing
	 * one is missing, the string is being clipped.
	 */
	std::string Pseudoize(const std::string& text) const;

	/**
	 * Pseudoizes a `<text>` element's markup, leaving `<style>` tags and `{directive}` braces
	 * exactly as they were. Accenting a term tag would only prove that a corrupted tag does not
	 * resolve, which is not the question pseudo-localization asks.
	 */
	std::string PseudoizeMarkup(const std::string& markup) const;

private:
	std::optional<std::string> Lookup(const std::string& key) const;

	/** Swaps letters for recognisable accented stand-ins. No inflation and no brackets. */
	std::string AccentText(const std::string& text) const;

	/**
	 * Pads with filler words until the visible length reaches 140% of the original, then wraps
	 * the result in brackets. `current` counts only what a reader sees, which is not the same as
	 * the length of `body` once markup is in it.
	 */
	std::string Inflate(std::string body, size_t original, size_t current) const;

	Diagnostics& diagnostics;
	std::map<std::string, std::string> strings;
	std::string path;
	bool active = false;
	bool pseudo = false;
};

} // namespace uipreview

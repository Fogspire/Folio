#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <string>
#include <vector>

namespace Folio::TextMarkup
{
	/// A stretch of text sharing one set of style classes. What becomes a `<span>`.
	struct FRun
	{
		std::string Text;
		/// Space separated, in the order the tags opened, so `<angry><whisper>` reads
		/// `angry whisper` and the later one wins where they disagree.
		std::string Classes;
		/// A `{br}`, which carries no text and becomes a line break.
		bool bBreak = false;
		/// Where this run's first character sits in the parsed text, counting codepoints.
		int FirstChar = 0;
		int CharCount = 0;
	};

	/// Markup turned into runs to lay out and a schedule to reveal them by.
	struct FParsed
	{
		std::vector<FRun> Runs;

		/// Seconds to wait before each character appears, or a negative value where the reader's
		/// own rate applies. One entry per character of the parsed text.
		std::vector<float> Delays;
		/// Extra seconds to wait before each character, on top of whatever its delay is. A
		/// `{pause:}` lands on the character that follows it.
		std::vector<float> Pauses;

		/// Every run's text joined. What a translator, a word count and an accessibility check
		/// all want to see, and never what is laid out.
		std::string Plain;

		/// Style tag names and term tags used, deduplicated and in first-use order. Tools report
		/// against these; nothing at runtime reads them.
		std::vector<std::string> Styles;
		std::vector<std::string> Terms;

		/// Anything the author would want to know: a tag that never closed, an unknown directive,
		/// a term with no entry. The text still parses around each one.
		std::vector<std::string> Errors;

		int CharCount = 0;
	};

	/// Turns one line of authored markup into runs, resolving `{tag:}` against the loaded term
	/// tables.
	///
	/// The grammar is small and closes the same way twice: `<style>text</>` for a style, whose
	/// name becomes a class; `{directive:value}text{/}` for a directive that applies to what it
	/// encloses; `{tag:Some.Thing}` for a term, which substitutes and does not enclose. `</>` and
	/// `{/}` each close the nearest of their own kind, and `</style>` is accepted where naming it
	/// reads better. A backslash escapes any of `< > { } \`.
	///
	/// Directives are `delay` (seconds per character within it), `pause` (seconds once, before the
	/// next character) and `br` (a line break). Whitespace is collapsed, so markup can be indented
	/// in a document the way the rest of it is.
	FOLIOCORE_API FParsed Parse(const std::string& Markup);

	/// Runs joined back into text with no markup, for anything that needs to measure or read the
	/// line without laying it out.
	FOLIOCORE_API std::string StripMarkup(const std::string& Markup);
}

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/TextShapingContext.h>

namespace Folio
{
	class FFolioFontFace;

	/// One glyph of a shaped run, in em, so a single shaping serves every size the face is drawn
	/// at. Glyph indices, never codepoints: a shaper may produce one glyph from several characters
	/// or several from one, and the atlas is keyed the same way.
	struct FShapedGlyph
	{
		unsigned int GlyphIndex = 0;
		/// Includes the kerning against whatever follows, which is where a shaper puts it.
		float AdvanceEm = 0.0f;
		/// Where the glyph sits relative to the pen, y up. Nonzero for attached marks.
		float OffsetXEm = 0.0f;
		float OffsetYEm = 0.0f;
		/// Which codepoint of the shaped string this glyph came from, counting codepoints rather
		/// than bytes. A ligature carries the index of the first character it stands for and an
		/// attached mark carries its base's, so a caller walking the string one character at a
		/// time finds every glyph that belongs to that character and never splits a cluster.
		unsigned int CharIndex = 0;
	};

	using FShapedRun = std::vector<FShapedGlyph>;

	/// Turns a string into positioned glyphs.
	class IShaper
	{
	public:
		virtual ~IShaper() = default;

		/// Names the implementation, for the font report.
		virtual const char* GetName() const = 0;

		virtual void Shape(FFolioFontFace& Face, Rml::StringView String,
			const Rml::TextShapingContext& Context, FShapedRun& OutGlyphs) = 0;
	};

	/// One glyph per codepoint, kerned from the legacy `kern` table. What RmlUi's own font engine
	/// does, and correct for Latin, Greek, Cyrillic and CJK.
	std::unique_ptr<IShaper> MakeSimpleShaper();

	/// Full OpenType shaping. Null in a build without HarfBuzz.
	std::unique_ptr<IShaper> MakeHarfBuzzShaper();

	/// Shapes on demand and remembers the answer.
	///
	/// A shaped width is not the sum of its parts, so a substring cannot be measured by adding up
	/// glyphs measured before: it has to be shaped again. RmlUi breaks lines by asking for one
	/// token at a time and re-asking as it narrows an over-long word, and every font effect draws
	/// the line a second time, so the same few runs are shaped repeatedly within a single layout.
	class FShapedTextCache
	{
	public:
		explicit FShapedTextCache(std::unique_ptr<IShaper> InShaper);

		const FShapedRun& Get(FFolioFontFace& Face, Rml::StringView String,
			const Rml::TextShapingContext& Context);

		void Clear();

		const char* GetShaperName() const { return Shaper->GetName(); }
		std::size_t GetSize() const { return Runs.size(); }

	private:
		/// Dropped wholesale rather than evicted one at a time. Runs are cheap to reshape and the
		/// working set is one screen of text, so a cache this large only fills up when something
		/// is generating strings, where keeping the oldest is no better than keeping nothing.
		static constexpr std::size_t MaxEntries = 8192;

		std::unique_ptr<IShaper> Shaper;
		std::unordered_map<std::string, FShapedRun> Runs;
		/// Reused so a lookup does not allocate for anything short.
		std::string Key;
	};
}

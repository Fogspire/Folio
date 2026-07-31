#include "FolioShaper.h"

#include "FolioFontFace.h"

namespace Folio
{
	namespace
	{
		class FSimpleShaper final : public IShaper
		{
		public:
			const char* GetName() const override { return "kern"; }

			void Shape(FFolioFontFace& Face, Rml::StringView String, const Rml::TextShapingContext& Context,
				FShapedRun& OutGlyphs) override
			{
				const bool bKerning = Context.font_kerning != Rml::Style::FontKerning::None;
				unsigned int PriorGlyph = 0;

				for (auto It = Rml::StringIteratorU8(String); It; ++It)
				{
					const unsigned int GlyphIndex = Face.GetGlyphIndex(char32_t(*It));

					// A glyph with no field cannot be drawn or measured, so it contributes nothing,
					// including to the kerning of whatever follows it.
					const FGlyphBox* Box = Face.GetOrAppendGlyph(GlyphIndex);
					if (Box == nullptr)
					{
						continue;
					}

					FShapedGlyph& Glyph = OutGlyphs.emplace_back();
					Glyph.GlyphIndex = GlyphIndex;
					Glyph.AdvanceEm = Box->AdvanceEm;
					if (bKerning && PriorGlyph != 0)
					{
						Glyph.KerningEm = Face.GetKerningEm(PriorGlyph, GlyphIndex);
					}

					PriorGlyph = GlyphIndex;
				}
			}
		};
	}

	std::unique_ptr<IShaper> MakeSimpleShaper()
	{
		return std::make_unique<FSimpleShaper>();
	}

	FShapedTextCache::FShapedTextCache(std::unique_ptr<IShaper> InShaper)
		: Shaper(std::move(InShaper))
	{
	}

	const FShapedRun& FShapedTextCache::Get(FFolioFontFace& Face, Rml::StringView String,
		const Rml::TextShapingContext& Context)
	{
		// Everything that changes the answer goes in the key. The size does not: a run is shaped
		// in em, so one entry serves the same string at every size it is set at.
		Key.clear();
		const void* FaceAddress = &Face;
		Key.append(reinterpret_cast<const char*>(&FaceAddress), sizeof(FaceAddress));
		Key.push_back(char(int(Context.text_direction) * 2
			+ (Context.font_kerning == Rml::Style::FontKerning::None ? 1 : 0)));
		Key.append(Context.language);
		Key.push_back('\0');
		Key.append(String.begin(), String.size());

		if (const auto Found = Runs.find(Key); Found != Runs.end())
		{
			return Found->second;
		}

		if (Runs.size() >= MaxEntries)
		{
			Runs.clear();
		}

		FShapedRun& Run = Runs[Key];
		Shaper->Shape(Face, String, Context, Run);
		return Run;
	}

	void FShapedTextCache::Clear()
	{
		Runs.clear();
	}
}

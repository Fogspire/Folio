#include "FolioShaper.h"

#if FOLIO_WITH_HARFBUZZ

#include "FolioFontFace.h"

#include <hb.h>
#include <hb-ot.h>

namespace Folio
{
	namespace
	{
		/// Full OpenType shaping: ligatures, contextual forms, mark attachment and GPOS kerning,
		/// which is the only kerning a modern face actually ships.
		///
		/// Shaped in font units rather than at a pixel size. The fields the glyphs are drawn from
		/// are size-independent, so the positions are too, and one shaping serves 13 dp and 96 dp
		/// alike. It also means no hinting is involved, which is deliberate and matches how the
		/// advances in the atlas were measured.
		class FHarfBuzzShaper final : public IShaper
		{
		public:
			FHarfBuzzShaper() : Buffer(hb_buffer_create()) {}

			~FHarfBuzzShaper() override
			{
				for (const auto& Entry : Fonts)
				{
					hb_font_destroy(Entry.second.Font);
				}
				hb_buffer_destroy(Buffer);
			}

			const char* GetName() const override { return "harfbuzz"; }

			void Shape(FFolioFontFace& Face, Rml::StringView String, const Rml::TextShapingContext& Context,
				FShapedRun& OutGlyphs) override;

		private:
			struct FShapingFont
			{
				hb_font_t* Font = nullptr;
				float EmScale = 0.0f;
			};

			const FShapingFont* GetFont(FFolioFontFace& Face);

			void ShapeRun(const FShapingFont& Font, const char32_t* Codepoints, unsigned int Count,
				hb_script_t Script, hb_direction_t Direction, hb_language_t Language, bool bKerning,
				FShapedRun& OutGlyphs);

			std::unordered_map<const FFolioFontFace*, FShapingFont> Fonts;
			hb_buffer_t* Buffer = nullptr;

			std::vector<char32_t> Codepoints;
			std::vector<hb_script_t> Scripts;
		};

		bool IsWeakScript(hb_script_t Script)
		{
			return Script == HB_SCRIPT_COMMON || Script == HB_SCRIPT_INHERITED || Script == HB_SCRIPT_UNKNOWN;
		}

		const FHarfBuzzShaper::FShapingFont* FHarfBuzzShaper::GetFont(FFolioFontFace& Face)
		{
			if (const auto Found = Fonts.find(&Face); Found != Fonts.end())
			{
				return &Found->second;
			}

			const FFontData& Data = Face.GetData();
			if (!Data || Data->empty())
			{
				return nullptr;
			}

			// HarfBuzz reads the tables itself rather than through FreeType, so the blob has to
			// outlive the face it is handed to. A copy of the shared pointer rides along as the
			// blob's user data and is released with it.
			auto* Keep = new FFontData(Data);
			hb_blob_t* Blob = hb_blob_create(reinterpret_cast<const char*>((*Keep)->data()),
				(unsigned int)((*Keep)->size()), HB_MEMORY_MODE_READONLY, Keep,
				[](void* Payload) { delete static_cast<FFontData*>(Payload); });

			hb_face_t* HbFace = hb_face_create(Blob, (unsigned int)(Face.GetFaceIndex()));
			hb_blob_destroy(Blob);

			const unsigned int UnitsPerEm = hb_face_get_upem(HbFace);
			hb_font_t* Font = hb_font_create(HbFace);
			hb_face_destroy(HbFace);

			if (Font == nullptr || UnitsPerEm == 0)
			{
				hb_font_destroy(Font);
				return nullptr;
			}

			// Scale equal to the em means positions come back in design units.
			hb_font_set_scale(Font, int(UnitsPerEm), int(UnitsPerEm));
			hb_ot_font_set_funcs(Font);

			// FreeType was already told these coordinates, but HarfBuzz has its own view of the
			// file and would otherwise measure the default instance while the glyphs came from
			// another one.
			std::vector<hb_variation_t> Variations;
			Variations.reserve(Face.GetCoords().size());
			for (const FVariationAxisValue& Axis : Face.GetCoords())
			{
				hb_variation_t Variation;
				Variation.tag = HB_TAG(Axis.Tag[0], Axis.Tag[1], Axis.Tag[2], Axis.Tag[3]);
				Variation.value = Axis.Value;
				Variations.push_back(Variation);
			}
			if (!Variations.empty())
			{
				hb_font_set_variations(Font, Variations.data(), (unsigned int)(Variations.size()));
			}

			FShapingFont Entry;
			Entry.Font = Font;
			Entry.EmScale = 1.0f / float(UnitsPerEm);
			return &Fonts.emplace(&Face, Entry).first->second;
		}

		void FHarfBuzzShaper::ShapeRun(const FShapingFont& Font, const char32_t* Run, unsigned int Count,
			hb_script_t Script, hb_direction_t Direction, hb_language_t Language, bool bKerning,
			FShapedRun& OutGlyphs)
		{
			hb_buffer_clear_contents(Buffer);
			hb_buffer_add_utf32(Buffer, reinterpret_cast<const uint32_t*>(Run), int(Count), 0, int(Count));
			hb_buffer_set_direction(Buffer, Direction);
			hb_buffer_set_script(Buffer, Script);
			if (Language != HB_LANGUAGE_INVALID)
			{
				hb_buffer_set_language(Buffer, Language);
			}

			hb_feature_t Features[1];
			unsigned int FeatureCount = 0;
			if (!bKerning)
			{
				Features[0].tag = HB_TAG('k', 'e', 'r', 'n');
				Features[0].value = 0;
				Features[0].start = HB_FEATURE_GLOBAL_START;
				Features[0].end = HB_FEATURE_GLOBAL_END;
				FeatureCount = 1;
			}

			hb_shape(Font.Font, Buffer, FeatureCount > 0 ? Features : nullptr, FeatureCount);

			unsigned int GlyphCount = 0;
			const hb_glyph_info_t* Infos = hb_buffer_get_glyph_infos(Buffer, &GlyphCount);
			const hb_glyph_position_t* Positions = hb_buffer_get_glyph_positions(Buffer, &GlyphCount);
			if (Infos == nullptr || Positions == nullptr)
			{
				return;
			}

			OutGlyphs.reserve(OutGlyphs.size() + GlyphCount);
			for (unsigned int Index = 0; Index < GlyphCount; ++Index)
			{
				FShapedGlyph& Glyph = OutGlyphs.emplace_back();
				Glyph.GlyphIndex = Infos[Index].codepoint;
				Glyph.AdvanceEm = float(Positions[Index].x_advance) * Font.EmScale;
				Glyph.OffsetXEm = float(Positions[Index].x_offset) * Font.EmScale;
				Glyph.OffsetYEm = float(Positions[Index].y_offset) * Font.EmScale;
			}
		}

		void FHarfBuzzShaper::Shape(FFolioFontFace& Face, Rml::StringView String,
			const Rml::TextShapingContext& Context, FShapedRun& OutGlyphs)
		{
			const FShapingFont* Font = GetFont(Face);
			if (Font == nullptr)
			{
				return;
			}

			Codepoints.clear();
			for (auto It = Rml::StringIteratorU8(String); It; ++It)
			{
				Codepoints.push_back(char32_t(*It));
			}
			if (Codepoints.empty())
			{
				return;
			}

			// Shaping rules are per script, so a string that mixes them has to be split: shaping
			// Latin as if it were Han applies the wrong feature set to both. Punctuation, digits
			// and combining marks have no script of their own and stay with whatever they follow.
			hb_unicode_funcs_t* Unicode = hb_unicode_funcs_get_default();
			Scripts.assign(Codepoints.size(), HB_SCRIPT_COMMON);
			hb_script_t Current = HB_SCRIPT_COMMON;
			for (std::size_t Index = 0; Index < Codepoints.size(); ++Index)
			{
				const hb_script_t Script = hb_unicode_script(Unicode, hb_codepoint_t(Codepoints[Index]));
				if (!IsWeakScript(Script))
				{
					Current = Script;
				}
				Scripts[Index] = Current;
			}

			// A string that opens with punctuation has nothing to inherit from yet, so the first
			// real script is carried backwards over it.
			for (std::size_t Index = Scripts.size() - 1; Index > 0; --Index)
			{
				if (IsWeakScript(Scripts[Index - 1]))
				{
					Scripts[Index - 1] = Scripts[Index];
				}
			}

			hb_direction_t Direction = HB_DIRECTION_LTR;
			if (Context.text_direction == Rml::Style::Direction::Rtl)
			{
				Direction = HB_DIRECTION_RTL;
			}
			else if (Context.text_direction == Rml::Style::Direction::Auto)
			{
				Direction = hb_script_get_horizontal_direction(Scripts[0]);
				if (Direction != HB_DIRECTION_RTL)
				{
					Direction = HB_DIRECTION_LTR;
				}
			}

			const hb_language_t Language = Context.language.empty()
				? HB_LANGUAGE_INVALID
				: hb_language_from_string(Context.language.c_str(), int(Context.language.size()));

			const bool bKerning = Context.font_kerning != Rml::Style::FontKerning::None;

			// Runs of one script each, and a right-to-left string gets them back to front, which
			// is as far as this goes: a string that genuinely mixes the two directions needs the
			// bidi algorithm, which lives above a shaper.
			std::vector<std::pair<std::size_t, std::size_t>> Runs;
			for (std::size_t Begin = 0; Begin < Scripts.size();)
			{
				std::size_t End = Begin + 1;
				while (End < Scripts.size() && Scripts[End] == Scripts[Begin])
				{
					++End;
				}
				Runs.emplace_back(Begin, End);
				Begin = End;
			}

			if (Direction == HB_DIRECTION_RTL)
			{
				for (std::size_t Index = Runs.size(); Index > 0; --Index)
				{
					const auto& Run = Runs[Index - 1];
					ShapeRun(*Font, Codepoints.data() + Run.first, (unsigned int)(Run.second - Run.first),
						Scripts[Run.first], Direction, Language, bKerning, OutGlyphs);
				}
			}
			else
			{
				for (const auto& Run : Runs)
				{
					ShapeRun(*Font, Codepoints.data() + Run.first, (unsigned int)(Run.second - Run.first),
						Scripts[Run.first], Direction, Language, bKerning, OutGlyphs);
				}
			}
		}
	}

	std::unique_ptr<IShaper> MakeHarfBuzzShaper()
	{
		return std::make_unique<FHarfBuzzShaper>();
	}
}

#else

namespace Folio
{
	std::unique_ptr<IShaper> MakeHarfBuzzShaper()
	{
		return nullptr;
	}
}

#endif

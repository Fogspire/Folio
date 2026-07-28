#include "FolioFontFace.h"

#include "MsdfGlyphGenerator.h"

#include <RmlUi/Core/RenderManager.h>

#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H

namespace Folio
{
	FFolioFontFace::FFolioFontFace(FT_Library InLibrary, IMsdfTextureSink& InSink,
		const FMsdfAtlasParams& InParams, int InPageSize)
		: Library(InLibrary)
		, Sink(InSink)
		, Atlas(InSink, InParams, InPageSize)
	{
	}

	FFolioFontFace::~FFolioFontFace()
	{
		if (Face != nullptr)
		{
			FT_Done_Face(Face);
		}
	}

	bool FFolioFontFace::LoadFromMemory(std::vector<unsigned char> InFontData, int FaceIndex)
	{
		FontData = std::move(InFontData);
		if (FontData.empty())
		{
			return false;
		}

		if (FT_New_Memory_Face(Library, FontData.data(), FT_Long(FontData.size()), FaceIndex, &Face) != 0)
		{
			Face = nullptr;
			return false;
		}

		return FinishLoad();
	}

	bool FFolioFontFace::LoadFromFile(const std::string& Path, int FaceIndex)
	{
		if (FT_New_Face(Library, Path.c_str(), FaceIndex, &Face) != 0)
		{
			Face = nullptr;
			return false;
		}

		return FinishLoad();
	}

	bool FFolioFontFace::FinishLoad()
	{
		// Only outline faces can produce a distance field. A bitmap-only face has nothing to
		// measure a distance to.
		if (Face == nullptr || Face->units_per_EM == 0 || (Face->face_flags & FT_FACE_FLAG_SCALABLE) == 0)
		{
			return false;
		}

		const float EmScale = 1.0f / float(Face->units_per_EM);

		AscentEm = float(Face->ascender) * EmScale;
		DescentEm = -float(Face->descender) * EmScale;
		LineSpacingEm = float(Face->height) * EmScale;
		UnderlinePositionEm = -float(Face->underline_position) * EmScale;
		UnderlineThicknessEm = float(Face->underline_thickness) * EmScale;

		XHeightEm = 0.0f;
		if (const TT_OS2* Os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(Face, FT_SFNT_OS2)))
		{
			// version 0xFFFF marks a synthesised table, whose sxHeight is not meaningful.
			if (Os2->version != 0xFFFF && Os2->sxHeight > 0)
			{
				XHeightEm = float(Os2->sxHeight) * EmScale;
			}
		}
		if (XHeightEm <= 0.0f)
		{
			if (const unsigned int XGlyph = FT_Get_Char_Index(Face, U'x'))
			{
				if (FT_Load_Glyph(Face, XGlyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) == 0)
				{
					XHeightEm = float(Face->glyph->metrics.height) * EmScale;
				}
			}
		}

		bHasEllipsis = FT_Get_Char_Index(Face, 0x2026) != 0;

		FT_MM_Var* Variations = nullptr;
		bVariable = (Face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) != 0
			&& FT_Get_MM_Var(Face, &Variations) == 0;
		if (Variations != nullptr)
		{
			FT_Done_MM_Var(Library, Variations);
		}

		return true;
	}

	unsigned int FFolioFontFace::GetGlyphIndex(char32_t Codepoint) const
	{
		return Face != nullptr ? FT_Get_Char_Index(Face, FT_ULong(Codepoint)) : 0;
	}

	const FGlyphBox* FFolioFontFace::GetOrAppendGlyph(unsigned int GlyphIndex)
	{
		if (const auto Found = Glyphs.find(GlyphIndex); Found != Glyphs.end())
		{
			return &Found->second;
		}

		if (Face == nullptr)
		{
			return nullptr;
		}

		float AdvanceEm = 0.0f;
		GetGlyphAdvanceEm(Face, GlyphIndex, AdvanceEm);

		FMsdfGlyphBitmap Bitmap;
		if (!GenerateGlyphMsdf(Face, GlyphIndex, Atlas.GetParams(), Bitmap))
		{
			return nullptr;
		}

		FGlyphBox Box;
		if (!Atlas.Add(Bitmap, AdvanceEm, Box))
		{
			return nullptr;
		}

		++Version;
		return &Glyphs.emplace(GlyphIndex, Box).first->second;
	}

	float FFolioFontFace::GetKerningEm(unsigned int LeftGlyph, unsigned int RightGlyph) const
	{
		if (Face == nullptr || LeftGlyph == 0 || RightGlyph == 0 || !FT_HAS_KERNING(Face))
		{
			return 0.0f;
		}

		FT_Vector Kerning{};
		if (FT_Get_Kerning(Face, LeftGlyph, RightGlyph, FT_KERNING_UNSCALED, &Kerning) != 0)
		{
			return 0.0f;
		}

		return float(Kerning.x) / float(Face->units_per_EM);
	}

	Rml::Texture FFolioFontFace::GetPageTexture(Rml::RenderManager& RenderManager, int Page)
	{
		if (Page < 0 || Page >= Atlas.GetPageCount())
		{
			return Rml::Texture();
		}

		// Built here rather than when the page is allocated, so that ReleaseTextures can drop them
		// without losing the pages themselves. RmlUi releases font resources during shutdown and
		// then checks that no callback texture outlived it.
		while (int(PageTextures.size()) <= Page)
		{
			const int Index = int(PageTextures.size());
			const FMsdfHandle Handle = Atlas.GetPageHandle(Index);
			const int Size = Atlas.GetPageSize();

			// The handle is the renderer's own texture handle, so handing it straight to RmlUi is
			// what lets the render interface recognise this as a field rather than an image.
			PageTextures.push_back(std::make_unique<Rml::CallbackTextureSource>(
				[Handle, Size](const Rml::CallbackTextureInterface& TextureInterface) -> bool
				{
					TextureInterface.SetTextureHandle(Rml::TextureHandle(Handle), Rml::Vector2i(Size, Size));
					return true;
				}));
		}

		return PageTextures[std::size_t(Page)]->GetTexture(RenderManager);
	}

	void FFolioFontFace::ReleaseTextures()
	{
		PageTextures.clear();
	}
}

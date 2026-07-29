#include "FolioFontFace.h"

#include "MsdfGlyphGenerator.h"

#include "FolioCore/FolioTrace.h"

#include <cstring>

#include <RmlUi/Core/RenderManager.h>

#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H

namespace Folio
{
	namespace
	{
		/// FreeType reports axis bounds and takes coordinates as 16.16 fixed point.
		float FromFixed(FT_Fixed Value) { return float(Value) / 65536.0f; }
		FT_Fixed ToFixed(float Value) { return FT_Fixed(double(Value) * 65536.0 + 0.5); }

		void TagToString(FT_ULong Tag, char OutTag[5])
		{
			OutTag[0] = char((Tag >> 24) & 0xFF);
			OutTag[1] = char((Tag >> 16) & 0xFF);
			OutTag[2] = char((Tag >> 8) & 0xFF);
			OutTag[3] = char(Tag & 0xFF);
			OutTag[4] = '\0';
		}
	}

	FFolioFontFace::FFolioFontFace(FT_Library InLibrary, FMsdfAtlasSet& InAtlas)
		: Library(InLibrary)
		, Atlas(InAtlas)
	{
	}

	FFolioFontFace::~FFolioFontFace()
	{
		if (Face != nullptr)
		{
			FT_Done_Face(Face);
		}
	}

	bool FFolioFontFace::Load(FFontData InData, int FaceIndex, const FVariationCoords& InCoords)
	{
		FOLIO_TRACE_SCOPE("Folio/LoadFaceInstance");

		Data = std::move(InData);
		if (!Data || Data->empty())
		{
			return false;
		}

		if (FT_New_Memory_Face(Library, Data->data(), FT_Long(Data->size()), FaceIndex, &Face) != 0)
		{
			Face = nullptr;
			return false;
		}

		// Only outline faces can produce a distance field. A bitmap-only face has nothing to
		// measure a distance to.
		if (Face->units_per_EM == 0 || (Face->face_flags & FT_FACE_FLAG_SCALABLE) == 0)
		{
			return false;
		}

		ReadAxes();
		if (!InCoords.empty() && !ApplyCoords(InCoords))
		{
			return false;
		}

		// After the coordinates, because a variable face's metrics move with its axes.
		ReadMetrics();
		return true;
	}

	void FFolioFontFace::ReadAxes()
	{
		Axes.clear();
		if ((Face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) == 0)
		{
			return;
		}

		FT_MM_Var* Var = nullptr;
		if (FT_Get_MM_Var(Face, &Var) != 0 || Var == nullptr)
		{
			// A FreeType built without TT_CONFIG_OPTION_GX_VAR_SUPPORT fails here rather than at
			// link time, so a variable file quietly becomes its default instance.
			return;
		}

		for (FT_UInt Index = 0; Index < Var->num_axis; ++Index)
		{
			FVariationAxis Axis;
			TagToString(Var->axis[Index].tag, Axis.Tag);
			Axis.Minimum = FromFixed(Var->axis[Index].minimum);
			Axis.Default = FromFixed(Var->axis[Index].def);
			Axis.Maximum = FromFixed(Var->axis[Index].maximum);
			Axes.push_back(Axis);
		}

		FT_Done_MM_Var(Library, Var);
	}

	bool FFolioFontFace::ApplyCoords(const FVariationCoords& InCoords)
	{
		if (Axes.empty())
		{
			return true;
		}

		// The call takes a value for every axis in order, so start from the file's own defaults
		// and overwrite only the axes the caller named.
		std::vector<FT_Fixed> Values;
		Values.reserve(Axes.size());
		for (const FVariationAxis& Axis : Axes)
		{
			float Value = Axis.Default;
			for (const FVariationAxisValue& Wanted : InCoords)
			{
				if (std::strcmp(Wanted.Tag, Axis.Tag) == 0)
				{
					Value = Wanted.Value < Axis.Minimum ? Axis.Minimum
						: (Wanted.Value > Axis.Maximum ? Axis.Maximum : Wanted.Value);
					break;
				}
			}
			Values.push_back(ToFixed(Value));
		}

		if (FT_Set_Var_Design_Coordinates(Face, FT_UInt(Values.size()), Values.data()) != 0)
		{
			return false;
		}

		Coords = InCoords;
		return true;
	}

	void FFolioFontFace::ReadMetrics()
	{
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
	}

	unsigned int FFolioFontFace::GetGlyphIndex(char32_t Codepoint) const
	{
		return Face != nullptr ? FT_Get_Char_Index(Face, FT_ULong(Codepoint)) : 0;
	}

	void FFolioFontFace::SetCache(IMsdfCacheStore* InStore, std::string InKey)
	{
		CacheStore = InStore;
		CacheKey = std::move(InKey);
	}

	void FFolioFontFace::LoadCache()
	{
		FOLIO_TRACE_SCOPE("Folio/ReadGlyphCache");

		bCacheLoaded = true;
		if (CacheStore == nullptr || CacheKey.empty())
		{
			return;
		}

		std::vector<unsigned char> Data;
		if (!CacheStore->Read(CacheKey, Data))
		{
			return;
		}

		std::vector<FCachedGlyph> Cached;
		if (!DeserializeGlyphCache(Data, Atlas.GetAtlas().GetParams(), Cached))
		{
			return;
		}

		// Everything the cache holds goes in at once. It was all drawn last run, so it is very
		// likely wanted again, and packing is cheap next to generating.
		for (FCachedGlyph& Glyph : Cached)
		{
			if (Insert(Glyph) != nullptr)
			{
				++CacheHits;
				FOLIO_TRACE_COUNTER_ADD("Folio/GlyphsFromCache", 1);
				// Kept so a flush rewrites the whole instance. The file is one blob, so writing
				// only what is new would drop everything the cache already held.
				CacheGlyphs.push_back(std::move(Glyph));
			}
		}
	}

	void FFolioFontFace::FlushCache()
	{
		// Nothing new means the file on disk already says exactly this.
		if (CacheStore == nullptr || CacheKey.empty() || !bCacheDirty || CacheGlyphs.empty())
		{
			return;
		}

		FOLIO_TRACE_SCOPE("Folio/WriteGlyphCache");
		CacheStore->Write(CacheKey, SerializeGlyphCache(CacheGlyphs, Atlas.GetAtlas().GetParams()));
		bCacheDirty = false;
	}

	const FGlyphBox* FFolioFontFace::Insert(const FCachedGlyph& Glyph)
	{
		FMsdfGlyphBitmap Bitmap;
		Bitmap.Width = Glyph.Width;
		Bitmap.Height = Glyph.Height;
		Bitmap.Rgba = Glyph.Rgba;
		Bitmap.PlaneLeft = Glyph.PlaneLeft;
		Bitmap.PlaneBottom = Glyph.PlaneBottom;
		Bitmap.PlaneRight = Glyph.PlaneRight;
		Bitmap.PlaneTop = Glyph.PlaneTop;
		Bitmap.bEmpty = Glyph.bEmpty;

		FGlyphBox Box;
		if (!Atlas.GetAtlas().Add(Bitmap, Glyph.AdvanceEm, Box))
		{
			return nullptr;
		}

		++Version;
		return &Glyphs.emplace(Glyph.GlyphIndex, Box).first->second;
	}

	const FGlyphBox* FFolioFontFace::GetOrAppendGlyph(unsigned int GlyphIndex)
	{
		if (!bCacheLoaded)
		{
			LoadCache();
		}

		if (const auto Found = Glyphs.find(GlyphIndex); Found != Glyphs.end())
		{
			return &Found->second;
		}

		if (Face == nullptr)
		{
			return nullptr;
		}

		FCachedGlyph Generated;
		Generated.GlyphIndex = GlyphIndex;
		GetGlyphAdvanceEm(Face, GlyphIndex, Generated.AdvanceEm);

		FMsdfGlyphBitmap Bitmap;
		if (!GenerateGlyphMsdf(Face, GlyphIndex, Atlas.GetAtlas().GetParams(), Bitmap))
		{
			return nullptr;
		}

		Generated.PlaneLeft = Bitmap.PlaneLeft;
		Generated.PlaneBottom = Bitmap.PlaneBottom;
		Generated.PlaneRight = Bitmap.PlaneRight;
		Generated.PlaneTop = Bitmap.PlaneTop;
		Generated.Width = Bitmap.Width;
		Generated.Height = Bitmap.Height;
		Generated.bEmpty = Bitmap.bEmpty;
		Generated.Rgba = Bitmap.Rgba;

		const FGlyphBox* Box = Insert(Generated);
		if (Box != nullptr)
		{
			++CacheMisses;
			FOLIO_TRACE_COUNTER_ADD("Folio/GlyphsGenerated", 1);
			CacheGlyphs.push_back(std::move(Generated));
			bCacheDirty = true;
		}
		return Box;
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
		return Atlas.GetPageTexture(RenderManager, Page);
	}
}

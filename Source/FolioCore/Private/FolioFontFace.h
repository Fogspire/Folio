#pragma once

#include "FolioCore/MsdfTextureSink.h"
#include "FolioCore/MsdfTypes.h"

#include "MsdfAtlas.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Texture.h>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Folio
{
	/// One font file at one set of variation coordinates.
	///
	/// Everything here is size-independent. A face owns a single atlas whose fields serve every
	/// font size the face is ever asked for, which is the whole point of the approach: asking for
	/// 13 dp and 96 dp costs one atlas, not two.
	class FFolioFontFace
	{
	public:
		FFolioFontFace(FT_Library Library, IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams, int InPageSize);
		~FFolioFontFace();

		FFolioFontFace(const FFolioFontFace&) = delete;
		FFolioFontFace& operator=(const FFolioFontFace&) = delete;

		/// Takes a copy of the file bytes: FreeType keeps pointing at them for the face's life,
		/// and the caller's buffer usually does not live that long.
		bool LoadFromMemory(std::vector<unsigned char> FontData, int FaceIndex);
		bool LoadFromFile(const std::string& Path, int FaceIndex);

		/// True when the face carries an fvar table and FreeType was built with variation support.
		/// Probed rather than inferred from the version, because a FreeType without
		/// TT_CONFIG_OPTION_GX_VAR_SUPPORT fails the call instead of the build.
		bool IsVariable() const { return bVariable; }

		unsigned int GetGlyphIndex(char32_t Codepoint) const;

		/// Generates the field on first request. Null only if the glyph cannot be rendered at all.
		const FGlyphBox* GetOrAppendGlyph(unsigned int GlyphIndex);

		/// Kerning between two glyphs in em, from the legacy `kern` table. Modern faces kern
		/// through GPOS, which FreeType does not expose and which needs a shaper.
		float GetKerningEm(unsigned int LeftGlyph, unsigned int RightGlyph) const;

		Rml::Texture GetPageTexture(Rml::RenderManager& RenderManager, int Page);

		/// Drops the RmlUi-side textures while keeping the pages and the fields in them. RmlUi
		/// checks at shutdown that no callback texture outlived it, so these cannot simply be left
		/// to the destructor.
		void ReleaseTextures();

		/// Bumped whenever a glyph is appended. RmlUi rebuilds cached text geometry when the
		/// version it generated against no longer matches.
		int GetVersion() const { return Version; }

		const FMsdfAtlasParams& GetParams() const { return Atlas.GetParams(); }
		int GetGlyphCount() const { return int(Glyphs.size()); }
		int GetPageCount() const { return Atlas.GetPageCount(); }

		/// Face metrics in em, so a handle only has to multiply by its size.
		float GetAscentEm() const { return AscentEm; }
		float GetDescentEm() const { return DescentEm; }
		float GetLineSpacingEm() const { return LineSpacingEm; }
		float GetXHeightEm() const { return XHeightEm; }
		float GetUnderlinePositionEm() const { return UnderlinePositionEm; }
		float GetUnderlineThicknessEm() const { return UnderlineThicknessEm; }
		bool HasEllipsis() const { return bHasEllipsis; }

		FT_Face GetFtFace() const { return Face; }

	private:
		bool FinishLoad();

		FT_Library Library = nullptr;
		FT_Face Face = nullptr;
		std::vector<unsigned char> FontData;

		IMsdfTextureSink& Sink;
		FMsdfAtlas Atlas;

		std::unordered_map<unsigned int, FGlyphBox> Glyphs;
		/// One per atlas page. A CallbackTextureSource rather than a CallbackTexture because a
		/// process can have more than one render manager, and each needs its own resource.
		std::vector<std::unique_ptr<Rml::CallbackTextureSource>> PageTextures;

		int Version = 0;
		bool bVariable = false;
		bool bHasEllipsis = false;

		float AscentEm = 0.0f;
		float DescentEm = 0.0f;
		float LineSpacingEm = 0.0f;
		float XHeightEm = 0.0f;
		float UnderlinePositionEm = 0.0f;
		float UnderlineThicknessEm = 0.0f;
	};
}

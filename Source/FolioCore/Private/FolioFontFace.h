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
	/// One variable axis a font file declares, in design units.
	struct FVariationAxis
	{
		char Tag[5] = { 0, 0, 0, 0, 0 };
		float Minimum = 0.0f;
		float Default = 0.0f;
		float Maximum = 0.0f;
	};

	/// The bytes of a font file, shared by every instance cut from it.
	///
	/// FreeType keeps reading the buffer for the life of a face, and a variable file produces one
	/// face per coordinate set, so the data outlives any single face and is reference counted
	/// rather than copied per instance - Inter is close to a megabyte.
	using FFontData = std::shared_ptr<const std::vector<unsigned char>>;

	/// One font file at one fixed set of variation coordinates.
	///
	/// Everything here is size-independent. A face owns a single atlas whose fields serve every
	/// font size it is ever asked for, which is the point of the approach: 13 dp and 96 dp cost
	/// one atlas, not two. Two *weights* are two faces, because the outlines genuinely differ.
	class FFolioFontFace
	{
	public:
		FFolioFontFace(FT_Library Library, FMsdfAtlasSet& InAtlas);
		~FFolioFontFace();

		FFolioFontFace(const FFolioFontFace&) = delete;
		FFolioFontFace& operator=(const FFolioFontFace&) = delete;

		/// Opens the file and applies the coordinates. Passing none leaves the face at whatever
		/// instance the file names as its default.
		bool Load(FFontData Data, int FaceIndex, const FVariationCoords& Coords);

		/// Axes the file declares. Empty for a static face, or when FreeType was built without
		/// variation support - probed rather than assumed, because that build option is the
		/// difference between a variable font working and silently rendering one instance.
		const std::vector<FVariationAxis>& GetAxes() const { return Axes; }
		bool IsVariable() const { return !Axes.empty(); }
		const FVariationCoords& GetCoords() const { return Coords; }

		unsigned int GetGlyphIndex(char32_t Codepoint) const;

		/// Generates the field on first request. Null only if the glyph cannot be rendered at all.
		const FGlyphBox* GetOrAppendGlyph(unsigned int GlyphIndex);

		/// Kerning between two glyphs in em, from the legacy `kern` table. Modern faces kern
		/// through GPOS, which FreeType does not expose and which needs a shaper.
		float GetKerningEm(unsigned int LeftGlyph, unsigned int RightGlyph) const;

		Rml::Texture GetPageTexture(Rml::RenderManager& RenderManager, int Page);

		/// Bumped whenever a glyph is appended. RmlUi rebuilds cached text geometry when the
		/// version it generated against no longer matches.
		int GetVersion() const { return Version; }

		const FMsdfAtlasParams& GetParams() const { return Atlas.GetAtlas().GetParams(); }
		/// Glyphs this face contributed. The pages themselves are shared with every other face.
		int GetGlyphCount() const { return int(Glyphs.size()); }

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
		void ReadAxes();
		bool ApplyCoords(const FVariationCoords& InCoords);
		void ReadMetrics();

		FT_Library Library = nullptr;
		FT_Face Face = nullptr;
		FFontData Data;

		std::vector<FVariationAxis> Axes;
		FVariationCoords Coords;

		FMsdfAtlasSet& Atlas;

		std::unordered_map<unsigned int, FGlyphBox> Glyphs;

		int Version = 0;
		bool bHasEllipsis = false;

		float AscentEm = 0.0f;
		float DescentEm = 0.0f;
		float LineSpacingEm = 0.0f;
		float XHeightEm = 0.0f;
		float UnderlinePositionEm = 0.0f;
		float UnderlineThicknessEm = 0.0f;
	};
}

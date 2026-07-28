#pragma once

#include "FolioCore/MsdfTypes.h"

#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace Folio
{
	/// One glyph's field, ready to be packed into a page.
	///
	/// The texels are RGBA8 and are not premultiplied and not sRGB: three channels of multi-channel
	/// distance plus one channel of true signed distance, none of which is a colour.
	struct FMsdfGlyphBitmap
	{
		int Width = 0;
		int Height = 0;
		std::vector<unsigned char> Rgba;

		/// Quad bounds in em relative to the glyph origin, padded by half the field range so the
		/// quad covers everything the field describes. Y is up.
		float PlaneLeft = 0.0f;
		float PlaneBottom = 0.0f;
		float PlaneRight = 0.0f;
		float PlaneTop = 0.0f;

		/// True for a glyph with no outline, such as a space. Nothing to pack; it still advances.
		bool bEmpty = false;
	};

	/// Rasterizes one glyph's outline into a distance field.
	///
	/// The outline is loaded unscaled and unhinted. Hinting exists to snap stems to a specific
	/// pixel grid, which is the one thing a size-independent atlas cannot honour, and hinted
	/// advances do not scale linearly - so taking them here would reintroduce the size dependence
	/// the whole approach removes.
	bool GenerateGlyphMsdf(FT_Face Face, unsigned int GlyphIndex, const FMsdfAtlasParams& Params,
		FMsdfGlyphBitmap& OutBitmap);

	/// Horizontal advance in em for a glyph, from the unscaled outline.
	bool GetGlyphAdvanceEm(FT_Face Face, unsigned int GlyphIndex, float& OutAdvanceEm);
}

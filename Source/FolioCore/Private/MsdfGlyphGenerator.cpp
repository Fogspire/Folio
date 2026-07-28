#include "MsdfGlyphGenerator.h"

#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <cmath>

namespace Folio
{
	namespace
	{
		/// Corners shallower than this are not treated as corners. msdfgen's own default, and the
		/// value Slate uses.
		constexpr double CornerAngleThreshold = 3.0;

		unsigned char ToTexel(float Distance)
		{
			const float Scaled = 255.0f * Distance + 0.5f;
			if (Scaled <= 0.0f) return 0;
			if (Scaled >= 255.0f) return 255;
			return static_cast<unsigned char>(Scaled);
		}
	}

	bool GetGlyphAdvanceEm(FT_Face Face, unsigned int GlyphIndex, float& OutAdvanceEm)
	{
		if (Face == nullptr || Face->units_per_EM == 0)
		{
			return false;
		}

		if (FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0)
		{
			return false;
		}

		OutAdvanceEm = float(double(Face->glyph->metrics.horiAdvance) / double(Face->units_per_EM));
		return true;
	}

	bool GenerateGlyphMsdf(FT_Face Face, unsigned int GlyphIndex, const FMsdfAtlasParams& Params,
		FMsdfGlyphBitmap& OutBitmap)
	{
		using namespace msdfgen;

		OutBitmap = FMsdfGlyphBitmap();

		if (Face == nullptr || Face->units_per_EM == 0 || Params.PxPerEm <= 0 || Params.PxRange <= 0)
		{
			return false;
		}

		if (FT_Load_Glyph(Face, GlyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0)
		{
			return false;
		}

		const double UnitsPerEm = double(Face->units_per_EM);

		Shape Glyph;
		if (readFreetypeOutline(Glyph, &Face->glyph->outline, 1.0) != 0)
		{
			return false;
		}

		Glyph.normalize();

		// Deliberately no orientContours() here. readFreetypeOutline keeps the outline's own
		// winding, which TrueType already gets right for non-zero fill; re-deriving it from a
		// scanline instead fills the counters of glyphs whose topology the heuristic misreads
		// (4, £, Ç and most accented capitals, in Inter).
		//
		// resolveShapeGeometry() would additionally sort out self-intersections, but it only
		// exists when msdfgen is built against Skia, which neither build does. A font with
		// overlapping contours will show artifacts at the overlaps.

		if (Glyph.contours.empty())
		{
			OutBitmap.bEmpty = true;
			return true;
		}

		edgeColoringInkTrap(Glyph, CornerAngleThreshold);

		const Shape::Bounds Bounds = Glyph.getBounds();

		// Everything below is in font units until the projection scales it. The field has to reach
		// PxRange/2 outside the outline on every side, so the quad is the outline's bounds grown
		// by that much.
		const double UnitsPerPixel = UnitsPerEm / double(Params.PxPerEm);
		const double PadUnits = 0.5 * double(Params.PxRange) * UnitsPerPixel;

		const double Left = Bounds.l - PadUnits;
		const double Bottom = Bounds.b - PadUnits;
		const double Right = Bounds.r + PadUnits;
		const double Top = Bounds.t + PadUnits;

		const int Width = int(std::ceil((Right - Left) / UnitsPerPixel));
		const int Height = int(std::ceil((Top - Bottom) / UnitsPerPixel));
		if (Width <= 0 || Height <= 0)
		{
			OutBitmap.bEmpty = true;
			return true;
		}

		// Projection maps shape coordinates as (coord + translate) * scale, so the translation is
		// in font units and moves the padded bottom-left corner to the pixel origin. The distance
		// range is expressed in font units for the same reason.
		const SDFTransformation Transformation(
			Projection(Vector2(1.0 / UnitsPerPixel), Vector2(-Left, -Bottom)),
			Range(double(Params.PxRange) * UnitsPerPixel));

		Bitmap<float, 4> Field(Width, Height);
		generateMTSDF(Field, Glyph, Transformation);

		OutBitmap.Width = Width;
		OutBitmap.Height = Height;
		OutBitmap.Rgba.resize(std::size_t(Width) * std::size_t(Height) * 4);

		// msdfgen's origin is bottom-left and a texture's is top-left, so rows are flipped here
		// rather than in every consumer.
		for (int Y = 0; Y < Height; ++Y)
		{
			const int SourceY = Height - 1 - Y;
			for (int X = 0; X < Width; ++X)
			{
				const float* Pixel = Field(X, SourceY);
				unsigned char* Texel = &OutBitmap.Rgba[(std::size_t(Y) * std::size_t(Width) + std::size_t(X)) * 4];
				Texel[0] = ToTexel(Pixel[0]);
				Texel[1] = ToTexel(Pixel[1]);
				Texel[2] = ToTexel(Pixel[2]);
				Texel[3] = ToTexel(Pixel[3]);
			}
		}

		const float EmScale = float(1.0 / UnitsPerEm);
		OutBitmap.PlaneLeft = float(Left) * EmScale;
		OutBitmap.PlaneBottom = float(Bottom) * EmScale;
		OutBitmap.PlaneRight = float(Right) * EmScale;
		OutBitmap.PlaneTop = float(Top) * EmScale;

		return true;
	}
}

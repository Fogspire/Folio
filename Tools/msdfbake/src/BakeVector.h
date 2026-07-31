#pragma once

#include "FolioCore/MsdfTypes.h"
#include "MsdfGlyphGenerator.h"

#include <msdfgen.h>

#include <string>
#include <vector>

namespace msdfbake
{
	/// One SVG, read into the same kind of shape a glyph outline produces.
	struct FVectorShape
	{
		msdfgen::Shape Shape;

		double Width = 0.0;
		double Height = 0.0;

		/// What the drawing turned out to be, so the caller can say why a bake will disappoint.
		/// A distance field carries coverage and nothing else: one silhouette, coloured where it is
		/// drawn. More than one fill colour cannot survive that, and neither can a stroke, which
		/// has no area until it is outlined.
		int DistinctFills = 0;
		int StrokedPaths = 0;
	};

	/// A gradient paint, as nanosvg resolved it.
	///
	/// `Xform` is the inverse of the gradient's transform, so it maps a point in the drawing into
	/// gradient space: a linear gradient reads its position off y there, a radial off the distance
	/// from the origin. An elliptical gradient is a radial one whose transform is not uniform, so it
	/// needs nothing of its own.
	struct FVectorGradient
	{
		bool bRadial = false;
		float Xform[6] = {1, 0, 0, 1, 0, 0};
		/// 0 pad, 1 reflect, 2 repeat, matching nanosvg.
		int Spread = 0;
		/// Offset in 0..1 and 0xAABBGGRR, in order.
		std::vector<std::pair<float, unsigned int>> Stops;
	};

	/// One paint's worth of an SVG, in the order it is painted.
	struct FVectorLayer
	{
		FVectorShape Shape;
		/// Packed 0xAABBGGRR as nanosvg reports it; written out as #rrggbb. Meaningless when the
		/// paint is a gradient.
		unsigned int Colour = 0;
		/// Stroke geometry is kept apart from fills even at the same colour: the two are wound
		/// differently and unioning them subtracts rather than adds.
		bool bStroke = false;

		/// Set when the paint varies across the shape. The field still gives the edge; the colour
		/// comes from a baked raster rather than from image-color.
		bool bGradient = false;
		FVectorGradient Gradient;
	};

	/// Colours a layer's atlas tile from its gradient, with the field supplying the edge.
	///
	/// The result is straight RGBA at the field's own resolution: the gradient is evaluated per
	/// texel, and alpha is reconstructed from the field's distance channel so the silhouette keeps
	/// the same antialiased edge the MTSDF path would have produced.
	bool RasteriseLayerColour(const FVectorLayer& Layer, const Folio::FMsdfGlyphBitmap& Field,
		const Folio::FMsdfAtlasParams& Params, std::vector<unsigned char>& OutRgba);

	/// Reads an SVG's filled paths into a shape, mirroring y so it matches a font outline.
	bool LoadSvgShape(const std::string& Path, FVectorShape& OutShape, std::string& OutError);

	/// Something in an SVG that the bake will not carry, named so the author is not left guessing.
	struct FUnsupportedPaint
	{
		/// The markup that gave it away, e.g. "<image>".
		std::string Found;
		/// What it usually means, and what to do instead.
		std::string Explanation;
	};

	/// Scans an SVG's text for paints a distance field cannot represent.
	///
	/// Text rather than the parsed image, because nanosvg discards most of these before anyone can ask
	/// about them: an embedded bitmap, a pattern fill and a filter all simply vanish, and the bake comes
	/// out as an outline with no fill and no complaint. The one thing worse than an unsupported feature
	/// is one that fails silently.
	void FindUnsupportedPaints(const std::string& Path, std::vector<FUnsupportedPaint>& OutFound);

	/// Reads the same SVG as one shape per fill colour, painted back to front.
	///
	/// A single field is one silhouette, which is all an icon needs. A drawing with several fills
	/// is several silhouettes stacked, each tinted - still one atlas, still resolution
	/// independent, at the cost of one draw per colour.
	bool LoadSvgLayers(const std::string& Path, std::vector<FVectorLayer>& OutLayers, std::string& OutError);

	/// Normalises the drawing to one em on its longest side and generates its field.
	bool GenerateVectorField(FVectorShape& Shape, const Folio::FMsdfAtlasParams& Params,
		Folio::FMsdfGlyphBitmap& OutBitmap);

	/// `msdfbake vector`. Argv is the whole command line; parsing starts after the mode.
	int BakeVectors(int Argc, char** Argv);
}

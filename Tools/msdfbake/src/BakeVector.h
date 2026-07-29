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

	/// One fill colour's worth of an SVG, in the order it is painted.
	struct FVectorLayer
	{
		FVectorShape Shape;
		/// Packed 0xAABBGGRR as nanosvg reports it; written out as #rrggbb.
		unsigned int Colour = 0;
		/// Stroke geometry is kept apart from fills even at the same colour: the two are wound
		/// differently and unioning them subtracts rather than adds.
		bool bStroke = false;
	};

	/// Reads an SVG's filled paths into a shape, mirroring y so it matches a font outline.
	bool LoadSvgShape(const std::string& Path, FVectorShape& OutShape, std::string& OutError);

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

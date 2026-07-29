#pragma once

#include "FolioCore/MsdfTypes.h"
#include "MsdfGlyphGenerator.h"

#include <msdfgen.h>

#include <string>

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

	/// Reads an SVG's filled paths into a shape, mirroring y so it matches a font outline.
	bool LoadSvgShape(const std::string& Path, FVectorShape& OutShape, std::string& OutError);

	/// Normalises the drawing to one em on its longest side and generates its field.
	bool GenerateVectorField(FVectorShape& Shape, const Folio::FMsdfAtlasParams& Params,
		Folio::FMsdfGlyphBitmap& OutBitmap);

	/// `msdfbake vector`. Argv is the whole command line; parsing starts after the mode.
	int BakeVectors(int Argc, char** Argv);
}

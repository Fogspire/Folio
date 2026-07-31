#include "FolioCore/FolioCoreVersion.h"

#include "FolioCore/MsdfTypes.h"

#include <RmlUi/Core/Core.h>

#include <msdfgen.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#if FOLIO_WITH_HARFBUZZ
#include <hb.h>
#endif

namespace Folio
{
	namespace
	{
		/// Generates a field for a unit square and reports whether the centre reads as inside and
		/// a point well outside reads as outside.
		///
		/// This exists to pull real symbols out of msdfgen-core rather than only its headers. An
		/// unreferenced static library resolves nothing, so without a call like this a duplicate
		/// symbol against Slate's own copy of msdfgen would not surface until something else
		/// happened to reference it.
		bool RunMsdfSelfTest()
		{
			using namespace msdfgen;

			Shape Square;
			Contour& Outline = Square.addContour();
			Outline.addEdge(EdgeHolder(Point2(1, 1), Point2(3, 1)));
			Outline.addEdge(EdgeHolder(Point2(3, 1), Point2(3, 3)));
			Outline.addEdge(EdgeHolder(Point2(3, 3), Point2(1, 3)));
			Outline.addEdge(EdgeHolder(Point2(1, 3), Point2(1, 1)));

			Square.normalize();
			Square.orientContours();
			edgeColoringInkTrap(Square, 3.0);

			Bitmap<float, 4> Field(16, 16);
			const SDFTransformation Transformation(Projection(Vector2(4.0), Vector2(0.0)), Range(1.0));
			generateMTSDF(Field, Square, Transformation);

			const float* Centre = Field(8, 8);
			const float* Corner = Field(0, 0);
			return median(Centre[0], Centre[1], Centre[2]) > 0.5f
				&& median(Corner[0], Corner[1], Corner[2]) < 0.5f;
		}

		std::string GetFreeTypeVersion()
		{
			FT_Library Library = nullptr;
			if (FT_Init_FreeType(&Library) != 0)
			{
				return "unavailable";
			}

			FT_Int Major = 0, Minor = 0, Patch = 0;
			FT_Library_Version(Library, &Major, &Minor, &Patch);
			FT_Done_FreeType(Library);

			return std::to_string(Major) + "." + std::to_string(Minor) + "." + std::to_string(Patch);
		}

		std::string GetHarfBuzzVersion()
		{
#if FOLIO_WITH_HARFBUZZ
			return hb_version_string();
#else
			return "none";
#endif
		}
	}

	std::string GetCoreVersionString()
	{
		const FMsdfAtlasParams Defaults;

		return "engine=FolioMsdf"
			" rmlui=" + std::string(Rml::GetVersion())
			+ " freetype=" + GetFreeTypeVersion()
			+ " harfbuzz=" + GetHarfBuzzVersion()
			+ " pxem=" + std::to_string(Defaults.PxPerEm)
			+ " pxrange=" + std::to_string(Defaults.PxRange)
			+ " selftest=" + (RunMsdfSelfTest() ? "ok" : "FAILED");
	}
}

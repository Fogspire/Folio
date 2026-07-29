#include "BakeVector.h"

#include "BakeSink.h"
#include "MsdfAtlas.h"
#include "MsdfGlyphGenerator.h"

#include "FolioCore/MsdfSidecar.h"

#include <msdfgen.h>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace msdfbake
{
	namespace
	{
		/// Cubic segments come out of nanosvg already flattened to beziers, so the shape is built
		/// from them directly rather than re-sampling into line segments.
		void AddPath(msdfgen::Shape& Shape, const NSVGpath& Path, double Height)
		{
			using namespace msdfgen;

			// SVG's y axis points down and msdfgen's points up, so y is mirrored about the
			// drawing's height as it is read. Doing it here means every consumer downstream sees
			// the same orientation a font outline has.
			const auto Point = [Height](const float* P) { return Point2(double(P[0]), Height - double(P[1])); };

			if (Path.npts < 4)
			{
				return;
			}

			Contour& Out = Shape.addContour();

			for (int Index = 0; Index + 3 < Path.npts; Index += 3)
			{
				const float* P = &Path.pts[Index * 2];
				Out.addEdge(EdgeHolder(Point(P), Point(P + 2), Point(P + 4), Point(P + 6)));
			}

			// An unclosed path still has to enclose an area to have an inside, so it is closed with
			// a straight run back to where it started.
			if (!Path.closed && Path.npts > 0)
			{
				const Point2 First = Point(&Path.pts[0]);
				const Point2 Last = Point(&Path.pts[(Path.npts - 1) * 2]);
				if (First != Last)
				{
					Out.addEdge(EdgeHolder(Last, First));
				}
			}
		}

		int CountDistinctFills(const NSVGimage& Image)
		{
			std::set<unsigned int> Fills;
			for (const NSVGshape* Shape = Image.shapes; Shape != nullptr; Shape = Shape->next)
			{
				if ((Shape->flags & NSVG_FLAGS_VISIBLE) != 0 && Shape->fill.type != NSVG_PAINT_NONE)
				{
					Fills.insert(Shape->fill.color);
				}
			}

			return int(Fills.size());
		}
	}

	bool LoadSvgShape(const std::string& Path, FVectorShape& OutShape, std::string& OutError)
	{
		// Units are px and the dpi only matters for physical units, which UI art does not use.
		NSVGimage* Image = nsvgParseFromFile(Path.c_str(), "px", 96.0f);
		if (Image == nullptr)
		{
			OutError = "cannot read " + Path;
			return false;
		}

		if (Image->width <= 0.0f || Image->height <= 0.0f)
		{
			nsvgDelete(Image);
			OutError = Path + " has no width or height";
			return false;
		}

		OutShape.Width = double(Image->width);
		OutShape.Height = double(Image->height);
		OutShape.DistinctFills = CountDistinctFills(*Image);
		OutShape.StrokedPaths = 0;

		msdfgen::Shape& Shape = OutShape.Shape;
		Shape = msdfgen::Shape();

		for (const NSVGshape* Source = Image->shapes; Source != nullptr; Source = Source->next)
		{
			if ((Source->flags & NSVG_FLAGS_VISIBLE) == 0)
			{
				continue;
			}

			if (Source->stroke.type != NSVG_PAINT_NONE)
			{
				++OutShape.StrokedPaths;
			}

			// A stroke has no area of its own until it is outlined, and nanosvg does not outline
			// one. Only fills contribute, which is why a drawing that is mostly strokes bakes to
			// far less than it looks like it should.
			if (Source->fill.type == NSVG_PAINT_NONE)
			{
				continue;
			}

			for (const NSVGpath* Path = Source->paths; Path != nullptr; Path = Path->next)
			{
				AddPath(Shape, *Path, OutShape.Height);
			}
		}

		nsvgDelete(Image);

		if (Shape.contours.empty())
		{
			OutError = Path + " has no filled paths to bake";
			return false;
		}

		return true;
	}

	bool GenerateVectorField(FVectorShape& Shape, const Folio::FMsdfAtlasParams& Params,
		Folio::FMsdfGlyphBitmap& OutBitmap)
	{
		// The drawing is normalised so its longest side is one em. An icon then behaves exactly
		// like a glyph: sized in em, packed into the same atlas, resolved by the same shader.
		const double UnitsPerEm = std::max(Shape.Width, Shape.Height);

		return Folio::GenerateShapeMsdf(&Shape.Shape, UnitsPerEm, Params, OutBitmap);
	}

	namespace
	{
		/// Markers so the block can be rewritten without disturbing anything hand-written around
		/// it, the same contract pack_atlas.py follows for the raster sheet.
		const char* const SpritesheetBegin = "/* BEGIN msdfbake vectors */";
		const char* const SpritesheetEnd = "/* END msdfbake vectors */";

		bool TakeValue(int Argc, char** Argv, int& Index, std::string& OutValue)
		{
			if (Index + 1 >= Argc)
			{
				return false;
			}

			OutValue = Argv[++Index];
			return true;
		}

		std::string StemOf(const std::string& Path)
		{
			const std::size_t Slash = Path.find_last_of("/\\");
			const std::string Name = Slash == std::string::npos ? Path : Path.substr(Slash + 1);

			const std::size_t Dot = Name.find('.');
			return Dot == std::string::npos ? Name : Name.substr(0, Dot);
		}

		bool WriteSpritesheet(const std::string& RcssPath, const std::string& Block, std::string& OutError)
		{
			std::string Existing;
			{
				std::ifstream File(RcssPath, std::ios::binary);
				if (File)
				{
					Existing.assign(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
				}
			}

			const std::size_t Begin = Existing.find(SpritesheetBegin);
			const std::size_t End = Existing.find(SpritesheetEnd);

			if (Begin != std::string::npos && End != std::string::npos && End > Begin)
			{
				Existing.replace(Begin, End + std::strlen(SpritesheetEnd) - Begin, Block);
			}
			else
			{
				if (!Existing.empty() && Existing.back() != '\n')
				{
					Existing += '\n';
				}

				Existing += Block;
				Existing += '\n';
			}

			std::ofstream File(RcssPath, std::ios::binary);
			if (!File)
			{
				OutError = "cannot write " + RcssPath;
				return false;
			}

			File << Existing;
			return true;
		}
	}

	int BakeVectors(int Argc, char** Argv)
	{
		using namespace Folio;

		std::vector<std::string> Inputs;
		std::string OutputDir, RcssPath, Name = "vectors", Value;
		FMsdfAtlasParams Params;
		int PageSize = 1024;

		for (int Index = 2; Index < Argc; ++Index)
		{
			const std::string Arg = Argv[Index];
			bool Ok = true;

			if (Arg == "--in")            { Ok = TakeValue(Argc, Argv, Index, Value); Inputs.push_back(Value); }
			else if (Arg == "--out")      Ok = TakeValue(Argc, Argv, Index, OutputDir);
			else if (Arg == "--rcss")     Ok = TakeValue(Argc, Argv, Index, RcssPath);
			else if (Arg == "--name")     Ok = TakeValue(Argc, Argv, Index, Name);
			else if (Arg == "--px-em")    { Ok = TakeValue(Argc, Argv, Index, Value); Params.PxPerEm = std::atoi(Value.c_str()); }
			else if (Arg == "--px-range") { Ok = TakeValue(Argc, Argv, Index, Value); Params.PxRange = std::atoi(Value.c_str()); }
			else if (Arg == "--page")     { Ok = TakeValue(Argc, Argv, Index, Value); PageSize = std::atoi(Value.c_str()); }
			else
			{
				std::fprintf(stderr, "msdfbake: error: unknown option %s\n", Arg.c_str());
				return 2;
			}

			if (!Ok)
			{
				std::fprintf(stderr, "msdfbake: error: %s needs a value\n", Arg.c_str());
				return 2;
			}
		}

		if (Inputs.empty() || OutputDir.empty())
		{
			std::fprintf(stderr, "msdfbake: error: vector needs at least one --in, and --out\n");
			return 2;
		}

		if (Params.PxPerEm <= 0 || Params.PxRange <= 0 || PageSize <= 0)
		{
			std::fprintf(stderr, "msdfbake: error: px-em, px-range and page must be positive\n");
			return 2;
		}

		BakeSink Sink;
		FMsdfAtlas Atlas(Sink, Params, PageSize);

		struct FPacked
		{
			std::string Name;
			FGlyphBox Box;
		};

		std::vector<FPacked> Sprites;
		int Failed = 0;

		for (const std::string& Input : Inputs)
		{
			FVectorShape Drawing;
			std::string Error;

			if (!LoadSvgShape(Input, Drawing, Error))
			{
				std::fprintf(stderr, "msdfbake: error: %s\n", Error.c_str());
				++Failed;
				continue;
			}

			const std::string SpriteName = StemOf(Input);

			// Said plainly rather than left for someone to work out from a bake that looks nothing
			// like the drawing. Neither is recoverable inside a single field.
			if (Drawing.DistinctFills > 1)
			{
				std::fprintf(stderr,
					"msdfbake: warning: %s has %d fill colours, and a distance field holds one "
					"silhouette. Every filled path merges into a single shape, coloured wherever it "
					"is drawn. Keep this as raster art, or split it into one file per colour.\n",
					SpriteName.c_str(), Drawing.DistinctFills);
			}

			if (Drawing.StrokedPaths > 0)
			{
				std::fprintf(stderr,
					"msdfbake: warning: %s has %d stroked path(s), which contribute nothing. A "
					"stroke has no area until it is outlined; convert strokes to fills first.\n",
					SpriteName.c_str(), Drawing.StrokedPaths);
			}

			FMsdfGlyphBitmap Bitmap;
			if (!GenerateVectorField(Drawing, Params, Bitmap) || Bitmap.bEmpty)
			{
				std::fprintf(stderr, "msdfbake: error: %s produced no field\n", SpriteName.c_str());
				++Failed;
				continue;
			}

			FGlyphBox Box;
			if (!Atlas.Add(Bitmap, 0.0f, Box))
			{
				std::fprintf(stderr, "msdfbake: error: %s did not fit the atlas\n", SpriteName.c_str());
				++Failed;
				continue;
			}

			Sprites.push_back({ SpriteName, Box });
		}

		if (Sprites.empty())
		{
			std::fprintf(stderr, "msdfbake: error: nothing was packed\n");
			return 1;
		}

		if (Atlas.GetPageCount() > 1)
		{
			std::fprintf(stderr,
				"msdfbake: error: the vector sheet spilled onto %d pages, and one @spritesheet "
				"names one image. Raise --page, or bake fewer icons at once.\n", Atlas.GetPageCount());
			return 1;
		}

		const std::string PngPath = OutputDir + "/" + Name + ".msdf.png";

		const std::string WriteError = Sink.WritePng(0, PngPath);
		if (!WriteError.empty())
		{
			std::fprintf(stderr, "msdfbake: error: %s: %s\n", PngPath.c_str(), WriteError.c_str());
			return 1;
		}

		FMsdfSidecar Sidecar;
		Sidecar.Version = MsdfSidecarVersion;
		Sidecar.Params = Params;

		{
			std::ofstream SidecarFile(MsdfSidecarPathFor(PngPath), std::ios::binary);
			if (!SidecarFile)
			{
				std::fprintf(stderr, "msdfbake: error: cannot write the sidecar for %s\n", PngPath.c_str());
				return 1;
			}

			SidecarFile << WriteMsdfSidecar(Sidecar);
		}

		if (!RcssPath.empty())
		{
			std::string Block = std::string(SpritesheetBegin) + "\n";
			Block += "/* Generated by msdfbake vector. Edits between the markers are overwritten. */\n";
			Block += "@spritesheet " + Name + "\n{\n";
			Block += "\tsrc: /ui/art/" + Name + ".msdf.png;\n";
			Block += "\tresolution: 1x;\n\n";

			for (const FPacked& Sprite : Sprites)
			{
				// The box carries a half-texel inset so bilinear sampling never reaches the gutter.
				// A sprite rectangle is whole texels, so the inset comes back out here.
				const int X = int(Sprite.Box.AtlasLeft);
				const int Y = int(Sprite.Box.AtlasTop);
				const int W = int(Sprite.Box.AtlasRight + 0.5f) - X;
				const int H = int(Sprite.Box.AtlasBottom + 0.5f) - Y;

				Block += "\t" + Sprite.Name + ": " + std::to_string(X) + "px " + std::to_string(Y)
					+ "px " + std::to_string(W) + "px " + std::to_string(H) + "px;\n";
			}

			Block += "}\n";
			Block += SpritesheetEnd;

			std::string Error;
			if (!WriteSpritesheet(RcssPath, Block, Error))
			{
				std::fprintf(stderr, "msdfbake: error: %s\n", Error.c_str());
				return 1;
			}

			std::printf("%s\n", RcssPath.c_str());
		}

		std::printf("%s\n", PngPath.c_str());
		std::printf("packed=%d failed=%d pxem=%d pxrange=%d\n",
			int(Sprites.size()), Failed, Params.PxPerEm, Params.PxRange);

		return Failed > 0 ? 1 : 0;
	}
}

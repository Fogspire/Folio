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

		/// Flattens a path's cubics into points, closely enough that the stroke's own width hides
		/// the error.
		void FlattenPath(const NSVGpath& Path, std::vector<msdfgen::Point2>& Out, double Height)
		{
			using namespace msdfgen;

			const auto At = [Height](const float* P) { return Point2(double(P[0]), Height - double(P[1])); };

			if (Path.npts < 4)
			{
				return;
			}

			constexpr int Steps = 8;

			Out.push_back(At(&Path.pts[0]));

			for (int Index = 0; Index + 3 < Path.npts; Index += 3)
			{
				const float* P = &Path.pts[Index * 2];
				const Point2 A = At(P), B = At(P + 2), C = At(P + 4), D = At(P + 6);

				for (int Step = 1; Step <= Steps; ++Step)
				{
					const double T = double(Step) / double(Steps);
					const double U = 1.0 - T;
					Out.push_back(U * U * U * A + 3.0 * U * U * T * B + 3.0 * U * T * T * C + T * T * T * D);
				}
			}
		}

		void AddDisc(msdfgen::Shape& Shape, msdfgen::Point2 Centre, double Radius)
		{
			using namespace msdfgen;

			// Four cubics is indistinguishable from a circle at any size a UI draws one.
			constexpr double K = 0.5522847498;

			Contour& Out = Shape.addContour();
			const Point2 R(Radius, 0.0), U(0.0, Radius);
			const Point2 E = Centre + R, N = Centre + U, W = Centre - R, S = Centre - U;

			Out.addEdge(EdgeHolder(E, E + U * K, N + R * K, N));
			Out.addEdge(EdgeHolder(N, N - R * K, W + U * K, W));
			Out.addEdge(EdgeHolder(W, W - U * K, S - R * K, S));
			Out.addEdge(EdgeHolder(S, S + R * K, E - U * K, E));
		}

		/// Outlines a stroke as area, because a distance field has no notion of a line.
		///
		/// Every segment becomes a rectangle and every joint a disc, and the union of that pile is
		/// the stroke. Skia's path operations do the union in resolveShapeGeometry, which is what
		/// makes the approach robust enough to be worth its simplicity - overlapping pieces are
		/// exactly what that call exists to resolve.
		///
		/// Joins and caps are always round. A miter would need the joint geometry proper; at UI
		/// sizes the difference is a fraction of a pixel on anything but a very sharp corner.
		void AddStroke(msdfgen::Shape& Shape, const NSVGshape& Source, double Height)
		{
			using namespace msdfgen;

			const double HalfWidth = 0.5 * double(Source.strokeWidth);
			if (HalfWidth <= 0.0)
			{
				return;
			}

			std::vector<Point2> Points;

			for (const NSVGpath* Path = Source.paths; Path != nullptr; Path = Path->next)
			{
				Points.clear();
				FlattenPath(*Path, Points, Height);

				if (Path->closed && Points.size() > 1)
				{
					Points.push_back(Points.front());
				}

				for (std::size_t Index = 0; Index + 1 < Points.size(); ++Index)
				{
					const Point2 A = Points[Index];
					const Point2 B = Points[Index + 1];

					const Vector2 Along = B - A;
					const double Length = Along.length();
					if (Length < 1e-9)
					{
						continue;
					}

					// Perpendicular, scaled to half the stroke: the rectangle that segment sweeps.
					const Vector2 Side = Vector2(-Along.y, Along.x) / Length * HalfWidth;

					// Counter-clockwise, matching the discs. Mixed winding does not union - the
					// path operations subtract one from the other and the stroke disappears
					// except where a disc happens to sit alone.
					Contour& Quad = Shape.addContour();
					Quad.addEdge(EdgeHolder(A - Side, B - Side));
					Quad.addEdge(EdgeHolder(B - Side, B + Side));
					Quad.addEdge(EdgeHolder(B + Side, A + Side));
					Quad.addEdge(EdgeHolder(A + Side, A - Side));
				}

				// A disc at every point covers the joins, and at the ends it is a round cap.
				for (const Point2& Point : Points)
				{
					AddDisc(Shape, Point, HalfWidth);
				}
			}
		}

		/// nanosvg has no clip support at all, and neither does a distance field: a clip is an
		/// intersection, and a field carries one shape. Detected by reading the file rather than
		/// asking the parser, because the parser has already discarded it by then.
		int CountClipPaths(const std::string& Path)
		{
			std::ifstream File(Path, std::ios::binary);
			if (!File)
			{
				return 0;
			}

			const std::string Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());

			int Count = 0;
			for (std::size_t At = Text.find("clip-path"); At != std::string::npos;
				At = Text.find("clip-path", At + 1))
			{
				++Count;
			}

			return Count;
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
				AddStroke(Shape, *Source, OutShape.Height);
			}

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

	bool LoadSvgLayers(const std::string& Path, std::vector<FVectorLayer>& OutLayers, std::string& OutError)
	{
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

		const double Width = double(Image->width);
		const double Height = double(Image->height);

		OutLayers.clear();

		// Grouped by colour but kept in paint order, so a later shape of a colour already seen
		// joins that colour's layer. Two layers of the same colour would draw identically anyway,
		// and merging them keeps the draw count down.
		for (const NSVGshape* Source = Image->shapes; Source != nullptr; Source = Source->next)
		{
			if ((Source->flags & NSVG_FLAGS_VISIBLE) == 0)
			{
				continue;
			}

			// A stroke carries its own colour, so it becomes its own layer rather than joining the
			// fill's.
			// Only the layer on top of the stack can be added to. Merging a colour with an earlier
			// layer of the same colour would reorder it past everything painted in between, and
			// paint order is the whole of what a stack of silhouettes encodes: eyes painted black
			// over a white head belong above it, not merged into the black outline beneath it.
			const auto LayerFor = [&OutLayers, Width, Height](unsigned int Colour, bool bStroke) -> FVectorLayer&
			{
				if (!OutLayers.empty() && OutLayers.back().Colour == Colour
					&& OutLayers.back().bStroke == bStroke)
				{
					return OutLayers.back();
				}

				OutLayers.push_back(FVectorLayer());
				FVectorLayer& Added = OutLayers.back();
				Added.Colour = Colour;
				Added.bStroke = bStroke;
				Added.Shape.Width = Width;
				Added.Shape.Height = Height;
				Added.Shape.DistinctFills = 1;
				return Added;
			};

			if (Source->fill.type != NSVG_PAINT_NONE)
			{
				FVectorLayer& Layer = LayerFor(Source->fill.color, false);
				for (const NSVGpath* SubPath = Source->paths; SubPath != nullptr; SubPath = SubPath->next)
				{
					AddPath(Layer.Shape.Shape, *SubPath, Height);
				}
			}

			if (Source->stroke.type != NSVG_PAINT_NONE)
			{
				AddStroke(LayerFor(Source->stroke.color, true).Shape.Shape, *Source, Height);
			}
		}

		nsvgDelete(Image);

		// A layer with no contours would bake to nothing and still cost a draw.
		OutLayers.erase(std::remove_if(OutLayers.begin(), OutLayers.end(),
			[](const FVectorLayer& Layer) { return Layer.Shape.Shape.contours.empty(); }), OutLayers.end());

		if (OutLayers.empty())
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

		// Not the glyph default. 48 px/em is ample for a glyph because a glyph fills its em, but a
		// drawing normalised to one em puts its whole extent in that many texels, and anything
		// finer than one of them disappears. A 2.5-unit stroke in a 400-unit drawing lands at a
		// third of a texel: it comes out as a dashed line, or as nothing.
		FMsdfAtlasParams Params;
		Params.PxPerEm = 256;
		Params.PxRange = 8;

		int PageSize = 2048;
		bool bLayers = false;

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
			else if (Arg == "--layers")   { bLayers = true; }
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
			/// Set when the sprite came from --layers, so the stylesheet can tint it.
			bool bHasColour = false;
			unsigned char R = 0, G = 0, B = 0;
			/// Where this layer sits inside the drawing, as fractions of it. A layer is packed at
			/// its own bounds, so without this they all stack at the origin and the art comes apart.
			float Left = 0.0f, Top = 0.0f, Width = 1.0f, Height = 1.0f;
		};

		std::vector<FPacked> Sprites;
		int Failed = 0;

		for (const std::string& Input : Inputs)
		{
			const std::string SpriteStem = StemOf(Input);
			std::string Error;

			if (bLayers)
			{
				std::vector<FVectorLayer> Layers;
				if (!LoadSvgLayers(Input, Layers, Error))
				{
					std::fprintf(stderr, "msdfbake: error: %s\n", Error.c_str());
					++Failed;
					continue;
				}

				int LayerIndex = 0;
				for (FVectorLayer& Layer : Layers)
				{
					FMsdfGlyphBitmap LayerBitmap;
					if (!GenerateVectorField(Layer.Shape, Params, LayerBitmap) || LayerBitmap.bEmpty)
					{
						++Failed;
						continue;
					}

					FGlyphBox LayerBox;
					if (!Atlas.Add(LayerBitmap, 0.0f, LayerBox))
					{
						std::fprintf(stderr, "msdfbake: error: a layer of %s did not fit the atlas\n",
							SpriteStem.c_str());
						++Failed;
						continue;
					}

					FPacked Packed;
					Packed.Name = SpriteStem + "_" + std::to_string(LayerIndex);
					Packed.Box = LayerBox;
					Packed.bHasColour = true;
					// nanosvg packs a fill colour as 0xAABBGGRR.
					Packed.R = (unsigned char)(Layer.Colour & 0xFF);
					Packed.G = (unsigned char)((Layer.Colour >> 8) & 0xFF);
					Packed.B = (unsigned char)((Layer.Colour >> 16) & 0xFF);

					// Plane bounds are in em from the drawing's own origin, and the drawing is one
					// em on its longest side, so dividing by its extent gives the fraction.
					const double Longest = std::max(Layer.Shape.Width, Layer.Shape.Height);
					const double SpanX = Layer.Shape.Width / Longest;
					const double SpanY = Layer.Shape.Height / Longest;

					Packed.Left = float(double(LayerBox.PlaneLeft) / SpanX);
					Packed.Top = float((SpanY - double(LayerBox.PlaneTop)) / SpanY);
					Packed.Width = float(double(LayerBox.PlaneRight - LayerBox.PlaneLeft) / SpanX);
					Packed.Height = float(double(LayerBox.PlaneTop - LayerBox.PlaneBottom) / SpanY);

					Sprites.push_back(Packed);
					++LayerIndex;
				}

				if (const int Clips = CountClipPaths(Input); Clips > 0)
				{
					std::fprintf(stderr,
						"msdfbake: warning: %s uses clip-path %d time(s), which is ignored. Clipping "
						"is an intersection and a field holds one shape, so the clipped geometry "
						"bakes unclipped and will cover things it should not. Apply the clip in the "
						"drawing program first.\n", SpriteStem.c_str(), Clips);
				}

				std::printf("%s: %d layer(s)\n", SpriteStem.c_str(), LayerIndex);
				continue;
			}

			FVectorShape Drawing;

			if (!LoadSvgShape(Input, Drawing, Error))
			{
				std::fprintf(stderr, "msdfbake: error: %s\n", Error.c_str());
				++Failed;
				continue;
			}

			const std::string& SpriteName = SpriteStem;

			// Said plainly rather than left for someone to work out from a bake that looks nothing
			// like the drawing. Neither is recoverable inside a single field.
			if (const int Clips = CountClipPaths(Input); Clips > 0)
			{
				std::fprintf(stderr,
					"msdfbake: warning: %s uses clip-path %d time(s), which is ignored. Clipping is "
					"an intersection and a field holds one shape, so the clipped geometry bakes "
					"unclipped and will cover things it should not. Apply the clip in the drawing "
					"program first.\n", SpriteName.c_str(), Clips);
			}

			if (Drawing.DistinctFills > 1)
			{
				std::fprintf(stderr,
					"msdfbake: warning: %s has %d fill colours, and a distance field holds one "
					"silhouette. Every filled path merges into a single shape, coloured wherever it "
					"is drawn. Keep this as raster art, or split it into one file per colour.\n",
					SpriteName.c_str(), Drawing.DistinctFills);
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
			// One class per layer, in paint order, carrying the colour the drawing used. Stacking
			// them reproduces the artwork; overriding image-color on any of them recolours it.
			bool bAnyColour = false;
			for (const FPacked& Sprite : Sprites)
			{
				if (!Sprite.bHasColour)
				{
					continue;
				}

				if (!bAnyColour)
				{
					Block += "\n/* Put these inside one position:relative box, in this order, to rebuild the art.\n";
					Block += "   Each carries where it sits in the drawing, so the box can be any size. */\n";
					bAnyColour = true;
				}

				char Rule[256] = {};
				std::snprintf(Rule, sizeof(Rule),
					".%s-ink { position: absolute; left: %.4f%%; top: %.4f%%; width: %.4f%%; height: %.4f%%; "
					"image-color: #%02x%02x%02x; }\n",
					Sprite.Name.c_str(), 100.0f * Sprite.Left, 100.0f * Sprite.Top,
					100.0f * Sprite.Width, 100.0f * Sprite.Height, Sprite.R, Sprite.G, Sprite.B);
				Block += Rule;
			}

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

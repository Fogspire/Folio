#include "BakeVector.h"

#include "BakeSink.h"
#include "MsdfAtlas.h"
#include "MsdfGlyphGenerator.h"

#include "FolioCore/MsdfSidecar.h"

#include <msdfgen.h>

#include <skia/core/SkPath.h>
#include <skia/pathops/SkPathOps.h>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace msdfbake
{
	namespace
	{
		/// Arcs are built by angle, and msdfgen brings no pi of its own.
		constexpr double kPi = 3.14159265358979323846;

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

		/// Appends an arc about Centre from From to To as line segments.
		///
		/// Line segments rather than cubics because these are joins and caps: a few degrees each,
		/// at a radius of half a stroke width. A cubic would be exact and unmeasurable.
		void AppendArc(std::vector<msdfgen::Point2>& Out, msdfgen::Point2 Centre, msdfgen::Point2 From,
			msdfgen::Point2 To, double Radius, bool bClockwise)
		{
			using namespace msdfgen;

			const Vector2 Start = From - Centre;
			const Vector2 End = To - Centre;

			double Angle = std::atan2(End.y, End.x) - std::atan2(Start.y, Start.x);
			while (Angle <= -kPi) { Angle += 2.0 * kPi; }
			while (Angle > kPi) { Angle -= 2.0 * kPi; }

			if (bClockwise && Angle > 0.0) { Angle -= 2.0 * kPi; }
			if (!bClockwise && Angle < 0.0) { Angle += 2.0 * kPi; }

			// About fifteen degrees a segment, well under a tenth of a pixel of chord error at any
			// stroke width a drawing uses.
			const int Steps = std::max(2, int(std::ceil(std::fabs(Angle) / (kPi / 12.0))));
			const double Base = std::atan2(Start.y, Start.x);

			for (int Step = 1; Step < Steps; ++Step)
			{
				const double T = Base + Angle * (double(Step) / double(Steps));
				Out.push_back(Centre + Vector2(std::cos(T), std::sin(T)) * Radius);
			}

			Out.push_back(To);
		}

		/// One side of a stroked polyline, with the joins put in.
		///
		/// Side is +1 or -1: which way the offset goes. The result runs in the same direction as
		/// the input, so closing a ring means walking the two sides in opposite directions.
		void OffsetSide(const std::vector<msdfgen::Point2>& Points, bool bClosed, double HalfWidth,
			double Side, char Join, double MiterLimit, std::vector<msdfgen::Point2>& Out)
		{
			using namespace msdfgen;

			const std::size_t Count = Points.size();
			const std::size_t Segments = bClosed ? Count : Count - 1;

			const auto UnitNormal = [&](std::size_t Index) -> Vector2
			{
				const Point2 A = Points[Index];
				const Point2 B = Points[(Index + 1) % Count];
				const Vector2 Along = B - A;
				const double Length = Along.length();
				if (Length < 1e-12)
				{
					return Vector2(0.0, 0.0);
				}
				return Vector2(-Along.y, Along.x) / Length * Side;
			};

			for (std::size_t Index = 0; Index < Segments; ++Index)
			{
				const Vector2 Unit = UnitNormal(Index);
				const Vector2 N = Unit * HalfWidth;
				const Point2 A = Points[Index];
				const Point2 B = Points[(Index + 1) % Count];

				Out.push_back(A + N);
				Out.push_back(B + N);

				// The junction with the next segment. An open path has none at its last vertex.
				if (!bClosed && Index + 2 >= Count)
				{
					continue;
				}

				const std::size_t NextIndex = (Index + 1) % Segments;
				const Vector2 NextUnit = UnitNormal(NextIndex);
				if (NextUnit.length() < 1e-12 || Unit.length() < 1e-12)
				{
					continue;
				}

				const Point2 From = B + N;
				const Point2 To = B + NextUnit * HalfWidth;
				if ((To - From).length() < 1e-12)
				{
					continue;
				}

				// Which way the path turns decides whether this side is the outside of the corner.
				// On the inside the two offsets cross, and the join is emitted anyway: the path
				// operations that run afterwards resolve the self-intersection, and deciding which
				// case is which here would buy nothing.
				const double Cross = crossProduct(Unit, NextUnit);

				if (Join == NSVG_JOIN_MITER)
				{
					// The bisector is the sum of the two unit normals; the miter reaches half the
					// width divided by the cosine of the half angle between them.
					const Vector2 Bisector = Unit + NextUnit;
					const double BisectorLength = Bisector.length();
					if (BisectorLength > 1e-12)
					{
						const Vector2 Direction = Bisector / BisectorLength;
						const double Cosine = dotProduct(Direction, Unit);
						if (Cosine > 1e-6 && 1.0 / Cosine <= MiterLimit)
						{
							Out.push_back(B + Direction * (HalfWidth / Cosine));
							continue;
						}
					}
					// Past the limit SVG says bevel, which is the direct line already implied.
				}
				else if (Join == NSVG_JOIN_ROUND)
				{
					AppendArc(Out, B, From, To, HalfWidth, Cross < 0.0);
					continue;
				}

				// Bevel: nothing to add. The next segment's first point closes the corner.
			}
		}

		/// Closes an open stroke's two sides, turning them into one loop.
		void AppendCap(std::vector<msdfgen::Point2>& Out, msdfgen::Point2 End, msdfgen::Point2 From,
			msdfgen::Point2 To, msdfgen::Vector2 Outward, double HalfWidth, char Cap)
		{
			using namespace msdfgen;

			if (Cap == NSVG_CAP_ROUND)
			{
				// The half turn has to go round the outside, through End + Outward. Taken the other
				// way it passes through the stroke instead and bites a crescent out of its end.
				AppendArc(Out, End, From, To, HalfWidth, crossProduct(From - End, Outward) < 0.0);
				return;
			}

			if (Cap == NSVG_CAP_SQUARE)
			{
				Out.push_back(From + Outward * HalfWidth);
				Out.push_back(To + Outward * HalfWidth);
			}

			// Butt: the two sides meet directly, which this last point does.
			Out.push_back(To);
		}

		/// Adds a closed contour through the given points, dropping any that repeat.
		void AddPolygon(msdfgen::Shape& Shape, const std::vector<msdfgen::Point2>& Points)
		{
			using namespace msdfgen;

			std::vector<Point2> Clean;
			Clean.reserve(Points.size());
			for (const Point2& Point : Points)
			{
				if (Clean.empty() || (Point - Clean.back()).length() > 1e-9)
				{
					Clean.push_back(Point);
				}
			}
			while (Clean.size() > 1 && (Clean.front() - Clean.back()).length() <= 1e-9)
			{
				Clean.pop_back();
			}

			if (Clean.size() < 3)
			{
				return;
			}

			Contour& Out = Shape.addContour();
			for (std::size_t Index = 0; Index < Clean.size(); ++Index)
			{
				Out.addEdge(EdgeHolder(Clean[Index], Clean[(Index + 1) % Clean.size()]));
			}
		}

		/// Outlines a stroke as area, because a distance field has no notion of a line.
		///
		/// One offset contour per side, with real joins and caps, rather than a rectangle per
		/// segment and a disc per vertex unioned together. The pile of pieces was simple and it
		/// failed on exactly the drawings that matter: a long ragged path produced hundreds of
		/// overlapping contours whose union came out solid, so a stroked outline baked as a
		/// silhouette and hid whatever it had been drawn over. It also forced round joins.
		///
		/// A closed path gives two loops, one per side, wound against each other, which reads as a
		/// ring under non-zero fill. An open path gives one loop: up one side, round the cap, back
		/// down the other.
		void AddStroke(msdfgen::Shape& Shape, const NSVGshape& Source, double Height)
		{
			using namespace msdfgen;

			const double HalfWidth = 0.5 * double(Source.strokeWidth);
			if (HalfWidth <= 0.0)
			{
				return;
			}

			const char Join = Source.strokeLineJoin;
			const char Cap = Source.strokeLineCap;
			const double MiterLimit = Source.miterLimit > 1.0 ? double(Source.miterLimit) : 1.0;

			std::vector<Point2> Points;
			std::vector<Point2> Left;
			std::vector<Point2> Right;

			for (const NSVGpath* Path = Source.paths; Path != nullptr; Path = Path->next)
			{
				Points.clear();
				FlattenPath(*Path, Points, Height);

				// A repeated point carries no direction, and a join needs one.
				Points.erase(std::unique(Points.begin(), Points.end(),
					[](const Point2& A, const Point2& B) { return (A - B).length() <= 1e-9; }), Points.end());

				const bool bClosed = Path->closed != 0;
				if (bClosed)
				{
					while (Points.size() > 1 && (Points.front() - Points.back()).length() <= 1e-9)
					{
						Points.pop_back();
					}
				}

				if (Points.size() < 2)
				{
					// A lone point is only visible under a round cap, where it is a dot.
					if (Points.size() == 1 && Cap == NSVG_CAP_ROUND)
					{
						AddDisc(Shape, Points.front(), HalfWidth);
					}
					continue;
				}

				Left.clear();
				Right.clear();
				OffsetSide(Points, bClosed, HalfWidth, 1.0, Join, MiterLimit, Left);
				OffsetSide(Points, bClosed, HalfWidth, -1.0, Join, MiterLimit, Right);

				if (bClosed)
				{
					// Wound against each other, so the area between them fills and the middle does
					// not. Which one is reversed does not matter, only that they disagree.
					std::reverse(Right.begin(), Right.end());
					AddPolygon(Shape, Left);
					AddPolygon(Shape, Right);
					continue;
				}

				// One loop: up the left side, round the far cap, back down the right, round the
				// near cap. A cap points away from the path along its end tangent.
				std::vector<Point2> Loop = Left;

				const Vector2 EndTangent = (Points.back() - Points[Points.size() - 2]).normalize();
				AppendCap(Loop, Points.back(), Left.back(), Right.back(), EndTangent, HalfWidth, Cap);

				for (std::size_t Index = Right.size() - 1; Index-- > 0;)
				{
					Loop.push_back(Right[Index]);
				}

				const Vector2 StartTangent = (Points[0] - Points[1]).normalize();
				AppendCap(Loop, Points.front(), Right.front(), Left.front(), StartTangent, HalfWidth, Cap);

				AddPolygon(Shape, Loop);
			}
		}

		/// nanosvg has no clip support at all, and neither does a distance field: a clip is an
		/// intersection, and a field carries one shape. Detected by reading the file rather than
		/// asking the parser, because the parser has already discarded it by then.
		/// What `clip-path` says, read from the file, because nanosvg throws it away.
		///
		/// A clip is an intersection and a field carries one shape, so it cannot be expressed as
		/// another layer. It can be applied at bake time though: Skia's path operations are already
		/// here for unioning a glyph's overlapping contours, and an intersection is the same call
		/// with a different operator.
		///
		/// nanosvg drops the relationship but still emits the clip path's own geometry as an
		/// ordinary shape, so nothing has to re-parse the SVG's path data. Only the relationship
		/// does: which shapes make up each clip, and which shapes each clip applies to.
		struct FClipPlan
		{
			/// Shape id to the clip it belongs to. These are geometry, and are never drawn.
			std::map<std::string, std::string> ClipMembers;
			/// Shape id to the clip that applies to it.
			std::map<std::string, std::string> Clipped;

			bool IsEmpty() const { return ClipMembers.empty() && Clipped.empty(); }
		};

		/// Ids of the `<path id="...">` elements between two offsets.
		void CollectIds(const std::string& Text, std::size_t From, std::size_t To,
			const std::string& Owner, std::map<std::string, std::string>& Out)
		{
			for (std::size_t At = Text.find("id=\"", From); At != std::string::npos && At < To;
				At = Text.find("id=\"", At + 1))
			{
				const std::size_t Begin = At + 4;
				const std::size_t End = Text.find('"', Begin);
				if (End == std::string::npos || End > To)
				{
					break;
				}

				// serif:id and similar carry the same value; the plain one is what nanosvg keeps.
				if (At >= 6 && Text.compare(At - 6, 6, "serif:") == 0)
				{
					continue;
				}

				Out[Text.substr(Begin, End - Begin)] = Owner;
			}
		}

		/// Finds the end of the element opened at Open, counting nested opens of the same tag.
		std::size_t FindElementEnd(const std::string& Text, const std::string& Tag, std::size_t Open)
		{
			const std::string OpenTag = "<" + Tag;
			const std::string CloseTag = "</" + Tag;

			int Depth = 0;
			for (std::size_t At = Open; At < Text.size();)
			{
				const std::size_t NextOpen = Text.find(OpenTag, At + 1);
				const std::size_t NextClose = Text.find(CloseTag, At + 1);

				if (NextClose == std::string::npos)
				{
					return Text.size();
				}

				if (NextOpen != std::string::npos && NextOpen < NextClose)
				{
					++Depth;
					At = NextOpen;
					continue;
				}

				if (Depth == 0)
				{
					return NextClose;
				}

				--Depth;
				At = NextClose;
			}

			return Text.size();
		}

		bool ReadClipPlan(const std::string& Path, FClipPlan& OutPlan)
		{
			std::ifstream File(Path, std::ios::binary);
			if (!File)
			{
				return false;
			}

			const std::string Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());

			// <clipPath id="A"> ... </clipPath>: everything inside is A's geometry.
			for (std::size_t At = Text.find("<clipPath"); At != std::string::npos;
				At = Text.find("<clipPath", At + 1))
			{
				const std::size_t IdAt = Text.find("id=\"", At);
				const std::size_t Head = Text.find('>', At);
				if (IdAt == std::string::npos || Head == std::string::npos || IdAt > Head)
				{
					continue;
				}

				const std::size_t IdEnd = Text.find('"', IdAt + 4);
				const std::string Id = Text.substr(IdAt + 4, IdEnd - IdAt - 4);
				const std::size_t Tail = FindElementEnd(Text, "clipPath", At);

				CollectIds(Text, Head, Tail, Id, OutPlan.ClipMembers);
			}

			// clip-path="url(#A)" on a group: everything inside is clipped by A.
			for (std::size_t At = Text.find("clip-path"); At != std::string::npos;
				At = Text.find("clip-path", At + 1))
			{
				const std::size_t Open = Text.rfind('<', At);
				const std::size_t Head = Text.find('>', At);
				const std::size_t Hash = Text.find('#', At);
				if (Open == std::string::npos || Head == std::string::npos || Hash == std::string::npos
					|| Hash > Head)
				{
					continue;
				}

				const std::size_t Close = Text.find(')', Hash);
				if (Close == std::string::npos || Close > Head)
				{
					continue;
				}

				const std::string Id = Text.substr(Hash + 1, Close - Hash - 1);

				// Only a group carries a clip over several shapes. On a single element the clip
				// covers just that element, and the same code handles it: the element is its own
				// extent between the tag's start and end.
				const bool bGroup = Text.compare(Open, 3, "<g ") == 0 || Text.compare(Open, 3, "<g>") == 0;
				const std::size_t Tail = bGroup ? FindElementEnd(Text, "g", Open) : Head;

				CollectIds(Text, Head, Tail, Id, OutPlan.Clipped);
			}

			// A shape cannot both be a clip and be clipped by it.
			for (const auto& Member : OutPlan.ClipMembers)
			{
				OutPlan.Clipped.erase(Member.first);
			}

			return !OutPlan.IsEmpty();
		}

		SkPoint ToSkia(const msdfgen::Point2 P)
		{
			return SkPoint::Make(SkScalar(P.x), SkScalar(P.y));
		}

		void ToSkiaPath(const msdfgen::Shape& Shape, SkPath& Out)
		{
			using namespace msdfgen;

			for (const Contour& Contour : Shape.contours)
			{
				if (Contour.edges.empty())
				{
					continue;
				}

				const EdgeSegment* Edge = Contour.edges.back();
				Out.moveTo(ToSkia(*Edge->controlPoints()));

				for (auto Next = Contour.edges.begin(); Next != Contour.edges.end(); Edge = *Next++)
				{
					const Point2* P = Edge->controlPoints();
					switch (Edge->type())
					{
					case (int)LinearSegment::EDGE_TYPE:    Out.lineTo(ToSkia(P[1])); break;
					case (int)QuadraticSegment::EDGE_TYPE: Out.quadTo(ToSkia(P[1]), ToSkia(P[2])); break;
					case (int)CubicSegment::EDGE_TYPE:     Out.cubicTo(ToSkia(P[1]), ToSkia(P[2]), ToSkia(P[3])); break;
					default: break;
					}
				}
			}
		}

		void FromSkiaPath(const SkPath& Path, msdfgen::Shape& Out)
		{
			using namespace msdfgen;

			Out.contours.clear();
			Contour* Current = &Out.addContour();

			SkPath::Iter Iterator(Path, true);
			SkPoint Points[4];

			for (SkPath::Verb Op; (Op = Iterator.next(Points)) != SkPath::kDone_Verb;)
			{
				const auto At = [&Points](const int Index)
				{
					return Point2(double(Points[Index].x()), double(Points[Index].y()));
				};

				switch (Op)
				{
				case SkPath::kMove_Verb:
					if (!Current->edges.empty())
					{
						Current = &Out.addContour();
					}
					break;
				case SkPath::kLine_Verb:  Current->addEdge(EdgeHolder(At(0), At(1))); break;
				case SkPath::kQuad_Verb:  Current->addEdge(EdgeHolder(At(0), At(1), At(2))); break;
				case SkPath::kCubic_Verb: Current->addEdge(EdgeHolder(At(0), At(1), At(2), At(3))); break;
				default: break;
				}
			}

			if (Current->edges.empty() && !Out.contours.empty())
			{
				Out.contours.pop_back();
			}
		}

		/// Keeps only the part of Subject that falls inside Clip.
		///
		/// This is what `clip-path` means, and the reason it can be honoured at bake time at all:
		/// the same library that unions a glyph's overlapping contours will intersect two shapes.
		bool ClipShape(msdfgen::Shape& Subject, const msdfgen::Shape& Clip)
		{
			if (Subject.contours.empty() || Clip.contours.empty())
			{
				return false;
			}

			SkPath SubjectPath, ClipPath, Result;
			ToSkiaPath(Subject, SubjectPath);
			ToSkiaPath(Clip, ClipPath);

			// Non-zero, matching how the rest of the pipeline reads a filled outline.
			SubjectPath.setFillType(SkPathFillType::kWinding);
			ClipPath.setFillType(SkPathFillType::kWinding);

			if (!Op(SubjectPath, ClipPath, kIntersect_SkPathOp, &Result))
			{
				return false;
			}

			FromSkiaPath(Result, Subject);
			return true;
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

		// What clip-path said, which nanosvg has already discarded. The geometry survives as
		// ordinary shapes, so this is only the relationship: which shapes are a clip, and which
		// shapes it applies to.
		FClipPlan Clips;
		ReadClipPlan(Path, Clips);

		std::map<std::string, msdfgen::Shape> ClipGeometry;
		if (!Clips.ClipMembers.empty())
		{
			for (const NSVGshape* Source = Image->shapes; Source != nullptr; Source = Source->next)
			{
				const auto Member = Clips.ClipMembers.find(Source->id);
				if (Member == Clips.ClipMembers.end())
				{
					continue;
				}

				msdfgen::Shape& Clip = ClipGeometry[Member->second];
				for (const NSVGpath* SubPath = Source->paths; SubPath != nullptr; SubPath = SubPath->next)
				{
					AddPath(Clip, *SubPath, OutShape.Height);
				}
			}
		}

		msdfgen::Shape& Shape = OutShape.Shape;
		Shape = msdfgen::Shape();

		for (const NSVGshape* Source = Image->shapes; Source != nullptr; Source = Source->next)
		{
			if ((Source->flags & NSVG_FLAGS_VISIBLE) == 0)
			{
				continue;
			}

			// A clip path's own geometry is a stencil, not ink. nanosvg does not know that and
			// emits it as an ordinary shape, which is how it ended up baked as a layer of its own.
			if (Clips.ClipMembers.count(Source->id) != 0)
			{
				continue;
			}

			const msdfgen::Shape* Clip = nullptr;
			if (const auto Clipped = Clips.Clipped.find(Source->id); Clipped != Clips.Clipped.end())
			{
				if (const auto Found = ClipGeometry.find(Clipped->second); Found != ClipGeometry.end())
				{
					Clip = &Found->second;
				}
			}

			const auto Emit = [&](msdfgen::Shape&& Built)
			{
				msdfgen::Shape Piece = std::move(Built);
				if (Clip != nullptr && !ClipShape(Piece, *Clip))
				{
					return;
				}

				for (msdfgen::Contour& Contour : Piece.contours)
				{
					Shape.contours.push_back(std::move(Contour));
				}
			};

			if (Source->stroke.type != NSVG_PAINT_NONE)
			{
				++OutShape.StrokedPaths;
				msdfgen::Shape Built;
				AddStroke(Built, *Source, OutShape.Height);
				Emit(std::move(Built));
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

		// What clip-path said, which nanosvg has already discarded. The geometry survives as
		// ordinary shapes, so this is only the relationship: which shapes are a clip, and which
		// shapes it applies to.
		FClipPlan Clips;
		ReadClipPlan(Path, Clips);

		std::map<std::string, msdfgen::Shape> ClipGeometry;
		if (!Clips.ClipMembers.empty())
		{
			for (const NSVGshape* Source = Image->shapes; Source != nullptr; Source = Source->next)
			{
				const auto Member = Clips.ClipMembers.find(Source->id);
				if (Member == Clips.ClipMembers.end())
				{
					continue;
				}

				msdfgen::Shape& Clip = ClipGeometry[Member->second];
				for (const NSVGpath* SubPath = Source->paths; SubPath != nullptr; SubPath = SubPath->next)
				{
					AddPath(Clip, *SubPath, Height);
				}
			}
		}

		// Grouped by colour but kept in paint order, so a later shape of a colour already seen
		// joins that colour's layer. Two layers of the same colour would draw identically anyway,
		// and merging them keeps the draw count down.
		for (const NSVGshape* Source = Image->shapes; Source != nullptr; Source = Source->next)
		{
			if ((Source->flags & NSVG_FLAGS_VISIBLE) == 0)
			{
				continue;
			}

			// A clip path's own geometry is a stencil, not ink. nanosvg does not know that and
			// emits it as an ordinary shape, which is how it ended up baked as a layer of its own.
			if (Clips.ClipMembers.count(Source->id) != 0)
			{
				continue;
			}

			const msdfgen::Shape* Clip = nullptr;
			if (const auto Clipped = Clips.Clipped.find(Source->id); Clipped != Clips.Clipped.end())
			{
				if (const auto Found = ClipGeometry.find(Clipped->second); Found != ClipGeometry.end())
				{
					Clip = &Found->second;
				}
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

			// A clipped shape is built on its own first, intersected, and only then added to its
			// layer: intersecting the layer would clip everything already painted into it.
			const auto Emit = [&](msdfgen::Shape& Target, msdfgen::Shape&& Built)
			{
				msdfgen::Shape Piece = std::move(Built);
				if (Clip != nullptr && !ClipShape(Piece, *Clip))
				{
					return;
				}

				for (msdfgen::Contour& Contour : Piece.contours)
				{
					Target.contours.push_back(std::move(Contour));
				}
			};

			if (Source->fill.type != NSVG_PAINT_NONE)
			{
				msdfgen::Shape Built;
				for (const NSVGpath* SubPath = Source->paths; SubPath != nullptr; SubPath = SubPath->next)
				{
					AddPath(Built, *SubPath, Height);
				}
				Emit(LayerFor(Source->fill.color, false).Shape.Shape, std::move(Built));
			}

			if (Source->stroke.type != NSVG_PAINT_NONE)
			{
				msdfgen::Shape Built;
				AddStroke(Built, *Source, Height);
				Emit(LayerFor(Source->stroke.color, true).Shape.Shape, std::move(Built));
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

		/// The drawing's own extent, as a sprite rectangle nothing ever draws.
		///
		/// RCSS has no aspect-ratio, and the only thing that carries one is an image element's
		/// intrinsic size. A hidden img on this sprite gives a layer stack its height from its
		/// width alone; without it the container collapses, because every layer is absolutely
		/// positioned and so adds nothing to its parent.
		///
		/// The rectangle is not packed and owns no atlas area. Nothing ever samples it, so it may
		/// sit over whatever else is there; only its proportions are read.
		struct FFrame
		{
			std::string Name;
			int Width = 1;
			int Height = 1;
		};

		std::vector<FPacked> Sprites;
		std::vector<FFrame> Frames;
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


				if (LayerIndex > 0)
				{
					// Scaled rather than taken literally: a drawing may be larger than a page, and
					// only the ratio is ever read. 512 holds the rounding error under a fifth of a
					// percent, far below anything visible in a stacked drawing.
					constexpr double FrameLongest = 512.0;
					const double Longest = std::max(Layers.front().Shape.Width, Layers.front().Shape.Height);
					const double Scale = Longest > 0.0 ? FrameLongest / Longest : 1.0;

					FFrame Frame;
					Frame.Name = SpriteStem + "_frame";
					Frame.Width = std::max(1, int(std::lround(Layers.front().Shape.Width * Scale)));
					Frame.Height = std::max(1, int(std::lround(Layers.front().Shape.Height * Scale)));
					Frames.push_back(Frame);
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

			if (!Frames.empty())
			{
				Block += "\n\t/* Each drawing's proportions. Nothing draws these; see below. */\n";
				for (const FFrame& Frame : Frames)
				{
					Block += "\t" + Frame.Name + ": 0px 0px " + std::to_string(Frame.Width) + "px "
						+ std::to_string(Frame.Height) + "px;\n";
				}
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
					Block += "\n/* One stack box, a hidden frame image, then these in order:\n";
					Block += "\n";
					Block += "     <div class=\"art-stack\" style=\"width: 240dp\">\n";
					Block += "       <img class=\"art-frame\" sprite=\"art_frame\"/>\n";
					Block += "       <img class=\"art_0-ink\" sprite=\"art_0\"/>\n";
					Block += "       <img class=\"art_1-ink\" sprite=\"art_1\"/>\n";
					Block += "     </div>\n";
					Block += "\n";
					Block += "   The frame is what gives the box a height. Every layer is absolutely positioned,\n";
					Block += "   so on its own the box would collapse and nothing would render; RCSS has no\n";
					Block += "   aspect-ratio, and an image element's intrinsic size is the only thing that\n";
					Block += "   carries one. Set the width and let the frame find the height - setting both as\n";
					Block += "   percentages squashes the art, because the two resolve against different bases. */\n";
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

			for (const FFrame& Frame : Frames)
			{
				const std::string Stem = Frame.Name.substr(0, Frame.Name.size() - 6);
				Block += "\n." + Stem + "-stack { position: relative; display: block; }\n";
				// Hidden rather than transparent: it must keep its box, which is the whole point
				// of it, and never cost a draw.
				Block += "." + Stem + "-frame { display: block; width: 100%; visibility: hidden; }\n";
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

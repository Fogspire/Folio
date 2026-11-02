#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

namespace Folio
{
	namespace Detail
	{
		struct FLineGraphProperties
		{
			bool bRegistered = false;
			Rml::PropertyId LowColour = Rml::PropertyId::Invalid;
			Rml::PropertyId MidColour = Rml::PropertyId::Invalid;
			Rml::PropertyId HighColour = Rml::PropertyId::Invalid;
			Rml::PropertyId AverageColour = Rml::PropertyId::Invalid;
			Rml::PropertyId LineWidth = Rml::PropertyId::Invalid;
			Rml::PropertyId DotSize = Rml::PropertyId::Invalid;
			Rml::PropertyId PointSize = Rml::PropertyId::Invalid;
			Rml::PropertyId FillOpacity = Rml::PropertyId::Invalid;
			Rml::PropertyId FillFade = Rml::PropertyId::Invalid;
			Rml::PropertyId FillPattern = Rml::PropertyId::Invalid;
			Rml::PropertyId FillSpacing = Rml::PropertyId::Invalid;
			Rml::PropertyId FillWidth = Rml::PropertyId::Invalid;
			Rml::PropertyId Curve = Rml::PropertyId::Invalid;
		};

		inline FLineGraphProperties& LineGraphProperties()
		{
			static FLineGraphProperties Properties;
			return Properties;
		}

		inline std::vector<std::unique_ptr<Rml::ElementInstancer>>& LineGraphInstancers()
		{
			static std::vector<std::unique_ptr<Rml::ElementInstancer>> Instancers;
			return Instancers;
		}

		/// Numbers separated by spaces or commas. Anything unreadable ends the list.
		inline void ParseNumbers(const Rml::String& Text, std::vector<float>& Out)
		{
			Out.clear();
			const char* Cursor = Text.c_str();
			while (*Cursor != '\0')
			{
				while (*Cursor == ' ' || *Cursor == ',' || *Cursor == '\t' || *Cursor == '\n')
				{
					++Cursor;
				}
				if (*Cursor == '\0')
				{
					break;
				}
				char* End = nullptr;
				const float Value = std::strtof(Cursor, &End);
				if (End == Cursor)
				{
					break;
				}
				Out.push_back(Value);
				Cursor = End;
			}
		}
	}

	/// `<line-graph>`: a line through a series of values, coloured per point, with a dot on the newest, an optional
	/// average line, an optional solid or patterned fill beneath and optional points on every value.
	///
	/// Attributes, all bindable with `data-attr-*`:
	///  - `values`: heights from `min` (bottom of the content box) to `max` (top), oldest first, spread evenly across the
	///    width.
	///  - `min`, `max`: the range `values`, `average` and `baseline` are in. 0 and 1 when left out.
	///  - `baseline`: the value the fill reaches to. The bottom of the content box when left out.
	///  - `tones`: one per value, from -1 to 1. -1 draws in `graph-low-color`, 0 in `graph-mid-color` and 1 in
	///    `graph-high-color`, blending smoothly between them. Missing tones draw at 0.
	///  - `average`: a height from 0 to 1 for a horizontal line in `graph-average-color`. Leave it out for none.
	///  - `length`: how many values fill the width. Fewer sit at the right edge, so a history that is still filling
	///    grows leftwards instead of stretching. Leave it out to spread whatever there is across the width.
	///
	/// Properties: `graph-low-color`, `graph-mid-color`, `graph-high-color`, `graph-average-color`, `graph-line-width`,
	/// `graph-dot-size` (the newest value's dot, 0 for none), `graph-point-size` (a dot on every value, 0 for none),
	/// `graph-fill-opacity` (the fill under the line, 0 for none), `graph-fill-fade` (how far the fill fades out toward
	/// the baseline, 0 to 1), `graph-fill-pattern` (`solid`, `hatch`, `crosshatch`, `columns`, `rows` or `dots`),
	/// `graph-fill-spacing` and `graph-fill-width` (the pattern's pitch and its line width or dot diameter) and
	/// `graph-curve` (`linear`, `smooth` or `step`).
	class FElementLineGraph : public Rml::Element
	{
	public:
		explicit FElementLineGraph(const Rml::String& Tag)
			: Rml::Element(Tag)
		{
		}

	protected:
		void OnAttributeChange(const Rml::ElementAttributes& Changed) override
		{
			Rml::Element::OnAttributeChange(Changed);

			if (Changed.find("values") != Changed.end())
			{
				Detail::ParseNumbers(GetAttribute<Rml::String>("values", ""), Values);
				bDirty = true;
			}
			if (Changed.find("tones") != Changed.end())
			{
				Detail::ParseNumbers(GetAttribute<Rml::String>("tones", ""), Tones);
				bDirty = true;
			}
			if (Changed.find("length") != Changed.end())
			{
				Length = GetAttribute<int>("length", 0);
				bDirty = true;
			}
			if (Changed.find("min") != Changed.end() || Changed.find("max") != Changed.end())
			{
				Min = GetAttribute<float>("min", 0.0f);
				Max = GetAttribute<float>("max", 1.0f);
				bDirty = true;
			}
			if (Changed.find("baseline") != Changed.end())
			{
				const Rml::String Text = GetAttribute<Rml::String>("baseline", "");
				bBaseline = !Text.empty();
				Baseline = bBaseline ? std::strtof(Text.c_str(), nullptr) : 0.0f;
				bDirty = true;
			}
			if (Changed.find("average") != Changed.end())
			{
				const Rml::String Text = GetAttribute<Rml::String>("average", "");
				bAverage = !Text.empty();
				Average = bAverage ? std::strtof(Text.c_str(), nullptr) : 0.0f;
				bDirty = true;
			}
		}

		void OnPropertyChange(const Rml::PropertyIdSet& Changed) override
		{
			Rml::Element::OnPropertyChange(Changed);
			bDirty = true;
		}

		void OnResize() override
		{
			bDirty = true;
		}

		void OnDpRatioChange() override
		{
			bDirty = true;
		}

		void OnRender() override
		{
			Rml::RenderManager* Manager = GetRenderManager();
			if (Manager == nullptr)
			{
				return;
			}
			if (bDirty)
			{
				bDirty = false;
				Rml::Mesh Mesh;
				Build(Mesh);
				Drawn = Mesh.indices.empty() ? Rml::Geometry() : Manager->MakeGeometry(std::move(Mesh));
			}
			if (Drawn)
			{
				Drawn.Render(GetAbsoluteOffset(Rml::BoxArea::Content));
			}
		}

	private:
		std::vector<float> Values;
		std::vector<float> Tones;
		float Average = 0.0f;
		bool bAverage = false;
		float Min = 0.0f;
		float Max = 1.0f;
		float Baseline = 0.0f;
		bool bBaseline = false;

		/// Keyword indices, in the order RegisterFolioLineGraph lists them.
		enum ECurve { CurveLinear, CurveSmooth, CurveStep };
		enum EPattern { PatternSolid, PatternHatch, PatternCrosshatch, PatternColumns, PatternRows, PatternDots };
		int Length = 0;
		bool bDirty = true;
		Rml::Geometry Drawn;

		/// Coverage fades to nothing over this many pixels past each edge, so lines and the dot are antialiased.
		static constexpr float Feather = 1.0f;

		Rml::Colourb GetColour(Rml::PropertyId Id, Rml::Colourb Fallback)
		{
			const Rml::Property* Value = GetProperty(Id);
			return Value != nullptr ? Value->Get<Rml::Colourb>() : Fallback;
		}

		float GetNumber(Rml::PropertyId Id, float Fallback)
		{
			const Rml::Property* Value = GetProperty(Id);
			return Value != nullptr ? Value->Get<float>() : Fallback;
		}

		int GetKeyword(Rml::PropertyId Id, int Fallback)
		{
			const Rml::Property* Value = GetProperty(Id);
			return Value != nullptr ? Value->Get<int>() : Fallback;
		}

		float GetLength(Rml::PropertyId Id, float Fallback)
		{
			const Rml::Property* Value = GetProperty(Id);
			return Value != nullptr ? ResolveLength(Value->GetNumericValue()) : Fallback;
		}

		static Rml::Colourb Lerp(Rml::Colourb A, Rml::Colourb B, float T)
		{
			const auto Channel = [T](Rml::byte From, Rml::byte To)
			{
				return Rml::byte(std::lround(float(From) + (float(To) - float(From)) * T));
			};
			return Rml::Colourb(Channel(A.red, B.red), Channel(A.green, B.green), Channel(A.blue, B.blue),
				Channel(A.alpha, B.alpha));
		}

		static void AddVertex(Rml::Mesh& Mesh, Rml::Vector2f Position, Rml::ColourbPremultiplied Colour)
		{
			Rml::Vertex Vertex;
			Vertex.position = Position;
			Vertex.colour = Colour;
			Vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
			Mesh.vertices.push_back(Vertex);
		}

		static void AddQuad(Rml::Mesh& Mesh, int A, int B, int C, int D)
		{
			Mesh.indices.insert(Mesh.indices.end(), { A, B, C, A, C, D });
		}

		/// A strip along Points: a solid core of HalfWidth either side of the line, then a feather to transparent.
		static void AddStrip(Rml::Mesh& Mesh, const std::vector<Rml::Vector2f>& Points,
			const std::vector<Rml::ColourbPremultiplied>& Colours, float HalfWidth)
		{
			const int Count = int(Points.size());
			const int First = int(Mesh.vertices.size());
			for (int Index = 0; Index < Count; ++Index)
			{
				const Rml::Vector2f In = Index > 0 ? (Points[Index] - Points[Index - 1]).Normalise() : Rml::Vector2f(0.0f);
				const Rml::Vector2f Out = Index + 1 < Count ? (Points[Index + 1] - Points[Index]).Normalise() : Rml::Vector2f(0.0f);
				Rml::Vector2f Tangent = (In + Out).Normalise();
				if (Tangent.SquaredMagnitude() < 0.5f)
				{
					Tangent = Index + 1 < Count ? Out : In;
				}
				const Rml::Vector2f Normal(-Tangent.y, Tangent.x);

				// Mitred: the offset grows as the join sharpens so the stroke keeps its width, capped so a spike
				// cannot throw a vertex across the graph.
				const Rml::Vector2f Segment = Index + 1 < Count ? Out : In;
				const float Cosine = std::abs(Normal.x * -Segment.y + Normal.y * Segment.x);
				const float Mitre = 1.0f / std::max(Cosine, 0.35f);

				const Rml::ColourbPremultiplied Solid = Colours[Index];
				const Rml::ColourbPremultiplied Clear(0, 0, 0, 0);
				const Rml::Vector2f Core = Normal * (HalfWidth * Mitre);
				const Rml::Vector2f Edge = Normal * ((HalfWidth + Feather) * Mitre);
				AddVertex(Mesh, Points[Index] + Edge, Clear);
				AddVertex(Mesh, Points[Index] + Core, Solid);
				AddVertex(Mesh, Points[Index] - Core, Solid);
				AddVertex(Mesh, Points[Index] - Edge, Clear);
			}
			for (int Index = 0; Index + 1 < Count; ++Index)
			{
				const int A = First + Index * 4;
				const int B = A + 4;
				AddQuad(Mesh, A, B, B + 1, A + 1);
				AddQuad(Mesh, A + 1, B + 1, B + 2, A + 2);
				AddQuad(Mesh, A + 2, B + 2, B + 3, A + 3);
			}
		}

		static void AddDot(Rml::Mesh& Mesh, Rml::Vector2f Centre, float Radius, Rml::ColourbPremultiplied Colour)
		{
			constexpr int Sides = 24;
			const int Middle = int(Mesh.vertices.size());
			AddVertex(Mesh, Centre, Colour);
			for (int Side = 0; Side < Sides; ++Side)
			{
				const float Angle = float(Side) * 2.0f * Rml::Math::RMLUI_PI / float(Sides);
				const Rml::Vector2f Direction(std::cos(Angle), std::sin(Angle));
				AddVertex(Mesh, Centre + Direction * Radius, Colour);
				AddVertex(Mesh, Centre + Direction * (Radius + Feather), Rml::ColourbPremultiplied(0, 0, 0, 0));
			}
			for (int Side = 0; Side < Sides; ++Side)
			{
				const int Inner = Middle + 1 + Side * 2;
				const int NextInner = Middle + 1 + ((Side + 1) % Sides) * 2;
				Mesh.indices.insert(Mesh.indices.end(), { Middle, Inner, NextInner });
				AddQuad(Mesh, Inner, Inner + 1, NextInner + 1, NextInner);
			}
		}

		static Rml::ColourbPremultiplied Scaled(Rml::ColourbPremultiplied Colour, float By)
		{
			const auto Channel = [By](Rml::byte Value) { return Rml::byte(std::lround(float(Value) * By)); };
			return Rml::ColourbPremultiplied(Channel(Colour.red), Channel(Colour.green), Channel(Colour.blue), Channel(Colour.alpha));
		}

		static Rml::ColourbPremultiplied Lerp(Rml::ColourbPremultiplied A, Rml::ColourbPremultiplied B, float T)
		{
			const auto Channel = [T](Rml::byte From, Rml::byte To)
			{
				return Rml::byte(std::lround(float(From) + (float(To) - float(From)) * T));
			};
			return Rml::ColourbPremultiplied(Channel(A.red, B.red), Channel(A.green, B.green), Channel(A.blue, B.blue),
				Channel(A.alpha, B.alpha));
		}

		static float Dot(Rml::Vector2f A, Rml::Vector2f B)
		{
			return A.x * B.x + A.y * B.y;
		}

		/// Where the line Normal . P = Offset crosses a convex polygon, ordered along Direction. False if it misses.
		static bool ClipToPolygon(const std::vector<Rml::Vector2f>& Polygon, Rml::Vector2f Normal, float Offset,
			Rml::Vector2f Direction, Rml::Vector2f& From, Rml::Vector2f& To)
		{
			float Lowest = FLT_MAX;
			float Highest = -FLT_MAX;
			const auto Consider = [&](Rml::Vector2f Point)
			{
				const float Along = Dot(Direction, Point);
				if (Along < Lowest)
				{
					Lowest = Along;
					From = Point;
				}
				if (Along > Highest)
				{
					Highest = Along;
					To = Point;
				}
			};
			const int Count = int(Polygon.size());
			for (int Index = 0; Index < Count; ++Index)
			{
				const Rml::Vector2f P = Polygon[Index];
				const Rml::Vector2f Q = Polygon[(Index + 1) % Count];
				const float DistanceP = Dot(Normal, P) - Offset;
				const float DistanceQ = Dot(Normal, Q) - Offset;
				if (DistanceP == 0.0f)
				{
					Consider(P);
				}
				if ((DistanceP < 0.0f && DistanceQ > 0.0f) || (DistanceP > 0.0f && DistanceQ < 0.0f))
				{
					Consider(P + (Q - P) * (DistanceP / (DistanceP - DistanceQ)));
				}
			}
			return Highest - Lowest > 0.01f;
		}

		/// Monotone cubic tangents (Fritsch-Carlson), so a smoothed curve never overshoots a peak or a step.
		static std::vector<float> MonotoneTangents(const std::vector<float>& Heights)
		{
			const int Count = int(Heights.size());
			std::vector<float> Slopes(Count - 1);
			for (int Index = 0; Index + 1 < Count; ++Index)
			{
				Slopes[Index] = Heights[Index + 1] - Heights[Index];
			}
			std::vector<float> Tangents(Count);
			Tangents.front() = Slopes.front();
			Tangents.back() = Slopes.back();
			for (int Index = 1; Index + 1 < Count; ++Index)
			{
				Tangents[Index] = Slopes[Index - 1] * Slopes[Index] <= 0.0f ? 0.0f : (Slopes[Index - 1] + Slopes[Index]) * 0.5f;
			}
			for (int Index = 0; Index + 1 < Count; ++Index)
			{
				if (Slopes[Index] == 0.0f)
				{
					Tangents[Index] = 0.0f;
					Tangents[Index + 1] = 0.0f;
					continue;
				}
				const float A = Tangents[Index] / Slopes[Index];
				const float B = Tangents[Index + 1] / Slopes[Index];
				const float Length = A * A + B * B;
				if (Length > 9.0f)
				{
					const float Scale = 3.0f / std::sqrt(Length);
					Tangents[Index] = Scale * A * Slopes[Index];
					Tangents[Index + 1] = Scale * B * Slopes[Index];
				}
			}
			return Tangents;
		}

		/// The area between the line and the baseline, one column per line segment.
		struct FFillArea
		{
			const std::vector<Rml::Vector2f>& Line;
			const std::vector<Rml::ColourbPremultiplied>& Colours;
			float Bottom;
			float Opacity;
			float Fade;

			int Columns() const
			{
				return int(Line.size()) - 1;
			}

			/// Convex pieces of a column: one quad, or two triangles where the line crosses the baseline.
			void Polygons(int Index, std::vector<std::vector<Rml::Vector2f>>& Out) const
			{
				Out.clear();
				const Rml::Vector2f A = Line[Index];
				const Rml::Vector2f B = Line[Index + 1];
				if (B.x - A.x < 0.001f)
				{
					return;
				}
				const float DepthA = A.y - Bottom;
				const float DepthB = B.y - Bottom;
				if (DepthA * DepthB < 0.0f)
				{
					const Rml::Vector2f Cross(A.x + (B.x - A.x) * (DepthA / (DepthA - DepthB)), Bottom);
					Out.push_back({ A, Cross, Rml::Vector2f(A.x, Bottom) });
					Out.push_back({ Cross, B, Rml::Vector2f(B.x, Bottom) });
				}
				else
				{
					Out.push_back({ A, B, Rml::Vector2f(B.x, Bottom), Rml::Vector2f(A.x, Bottom) });
				}
			}

			float TopAt(int Index, float X) const
			{
				const Rml::Vector2f A = Line[Index];
				const Rml::Vector2f B = Line[Index + 1];
				const float Span = B.x - A.x;
				return Span > 0.001f ? A.y + (B.y - A.y) * Rml::Math::Clamp((X - A.x) / Span, 0.0f, 1.0f) : A.y;
			}

			/// The line's colour above Point, faded by how far Point sits from the line toward the baseline.
			Rml::ColourbPremultiplied ColourAt(int Index, Rml::Vector2f Point) const
			{
				const Rml::Vector2f A = Line[Index];
				const Rml::Vector2f B = Line[Index + 1];
				const float Span = B.x - A.x;
				const float T = Span > 0.001f ? Rml::Math::Clamp((Point.x - A.x) / Span, 0.0f, 1.0f) : 0.0f;
				const float Top = A.y + (B.y - A.y) * T;
				const float Height = std::abs(Bottom - Top);
				const float Depth = Height > 0.001f ? Rml::Math::Clamp(std::abs(Point.y - Top) / Height, 0.0f, 1.0f) : 0.0f;
				return Scaled(Lerp(Colours[Index], Colours[Index + 1], T), Opacity * (1.0f - Fade * Depth));
			}
		};

		static void AddSolidFill(Rml::Mesh& Mesh, const FFillArea& Area)
		{
			std::vector<std::vector<Rml::Vector2f>> Pieces;
			for (int Index = 0; Index < Area.Columns(); ++Index)
			{
				Area.Polygons(Index, Pieces);
				for (const std::vector<Rml::Vector2f>& Piece : Pieces)
				{
					const int First = int(Mesh.vertices.size());
					for (const Rml::Vector2f& Corner : Piece)
					{
						AddVertex(Mesh, Corner, Area.ColourAt(Index, Corner));
					}
					for (int Corner = 1; Corner + 1 < int(Piece.size()); ++Corner)
					{
						Mesh.indices.insert(Mesh.indices.end(), { First, First + Corner, First + Corner + 1 });
					}
				}
			}
		}

		/// Parallel lines Spacing apart along Direction, clipped to the area. Pieces that meet at a column edge are
		/// joined into one strip, so a dense smoothed line leaves no seams.
		static void AddStripes(Rml::Mesh& Mesh, const FFillArea& Area, Rml::Vector2f Direction, float Spacing, float HalfWidth)
		{
			struct FStripe
			{
				std::vector<Rml::Vector2f> Points;
				std::vector<Rml::ColourbPremultiplied> Colours;
			};
			std::map<int, FStripe> Open;
			const auto Flush = [&Mesh, HalfWidth](FStripe& Stripe)
			{
				if (Stripe.Points.size() > 1)
				{
					AddStrip(Mesh, Stripe.Points, Stripe.Colours, HalfWidth);
				}
				Stripe.Points.clear();
				Stripe.Colours.clear();
			};

			const Rml::Vector2f Normal(-Direction.y, Direction.x);
			// Half a spacing off the origin, so a vertical stripe never lies along a column edge.
			const float Offset = Spacing * 0.5f;
			std::vector<std::vector<Rml::Vector2f>> Pieces;
			for (int Index = 0; Index < Area.Columns(); ++Index)
			{
				Area.Polygons(Index, Pieces);
				for (const std::vector<Rml::Vector2f>& Piece : Pieces)
				{
					float Lowest = FLT_MAX;
					float Highest = -FLT_MAX;
					for (const Rml::Vector2f& Corner : Piece)
					{
						Lowest = std::min(Lowest, Dot(Normal, Corner));
						Highest = std::max(Highest, Dot(Normal, Corner));
					}
					for (int Line = int(std::ceil((Lowest - Offset) / Spacing)); Line * Spacing + Offset <= Highest; ++Line)
					{
						Rml::Vector2f From;
						Rml::Vector2f To;
						if (!ClipToPolygon(Piece, Normal, Line * Spacing + Offset, Direction, From, To))
						{
							continue;
						}
						FStripe& Stripe = Open[Line];
						const bool bJoins = !Stripe.Points.empty() && (Stripe.Points.back() - From).SquaredMagnitude() < 0.0001f;
						if (!bJoins)
						{
							Flush(Stripe);
							Stripe.Points.push_back(From);
							Stripe.Colours.push_back(Area.ColourAt(Index, From));
						}
						Stripe.Points.push_back(To);
						Stripe.Colours.push_back(Area.ColourAt(Index, To));
					}
				}
			}
			for (auto& Entry : Open)
			{
				Flush(Entry.second);
			}
		}

		static void AddDots(Rml::Mesh& Mesh, const FFillArea& Area, float Spacing, float Radius)
		{
			const float Offset = Spacing * 0.5f;
			for (int Index = 0; Index < Area.Columns(); ++Index)
			{
				const float Left = Area.Line[Index].x;
				const float Right = Area.Line[Index + 1].x;
				for (float X = std::ceil((Left - Offset) / Spacing) * Spacing + Offset; X < Right; X += Spacing)
				{
					const float Top = Area.TopAt(Index, X);
					const float Upper = std::min(Top, Area.Bottom) + Radius;
					const float Lower = std::max(Top, Area.Bottom) - Radius;
					for (float Y = std::ceil((Upper - Offset) / Spacing) * Spacing + Offset; Y <= Lower; Y += Spacing)
					{
						const Rml::Vector2f Centre(X, Y);
						AddDot(Mesh, Centre, Radius, Area.ColourAt(Index, Centre));
					}
				}
			}
		}

		void Build(Rml::Mesh& Mesh)
		{
			const Detail::FLineGraphProperties& Ids = Detail::LineGraphProperties();
			const Rml::Vector2f Size = GetBox().GetSize(Rml::BoxArea::Content);
			if (Size.x <= 0.0f || Size.y <= 0.0f)
			{
				return;
			}

			const float Opacity = GetComputedValues().opacity();
			const float HalfWidth = std::max(GetLength(Ids.LineWidth, 2.0f), 0.0f) * 0.5f;
			const float Radius = std::max(GetLength(Ids.DotSize, 6.0f), 0.0f) * 0.5f;
			const float PointRadius = std::max(GetLength(Ids.PointSize, 0.0f), 0.0f) * 0.5f;
			const float FillOpacity = Rml::Math::Clamp(GetNumber(Ids.FillOpacity, 0.0f), 0.0f, 1.0f);
			const float FillFade = Rml::Math::Clamp(GetNumber(Ids.FillFade, 0.0f), 0.0f, 1.0f);
			const int FillPattern = GetKeyword(Ids.FillPattern, 0);
			const float FillSpacing = std::max(GetLength(Ids.FillSpacing, 6.0f), 2.0f);
			const float FillWidth = std::max(GetLength(Ids.FillWidth, 1.0f), 0.0f);
			const int Curve = GetKeyword(Ids.Curve, 0);
			const Rml::Colourb Low = GetColour(Ids.LowColour, Rml::Colourb(229, 83, 75));
			const Rml::Colourb Mid = GetColour(Ids.MidColour, Rml::Colourb(78, 161, 243));
			const Rml::Colourb High = GetColour(Ids.HighColour, Rml::Colourb(87, 211, 140));

			// Inset by the largest dot, so neither it nor the stroke is cut off at the edges.
			const float Inset = std::max({ Radius, PointRadius, HalfWidth }) + Feather;
			const float Range = Max != Min ? Max - Min : 1.0f;
			const auto Normalise = [this, Range](float Value) { return (Value - Min) / Range; };
			const auto Place = [&Size, Inset](float Fraction, float Height)
			{
				return Rml::Vector2f(Inset + Fraction * (Size.x - 2.0f * Inset),
					Inset + (1.0f - Rml::Math::Clamp(Height, 0.0f, 1.0f)) * (Size.y - 2.0f * Inset));
			};
			const auto ToneColour = [&Low, &Mid, &High, Opacity](float Tone)
			{
				Tone = Rml::Math::Clamp(Tone, -1.0f, 1.0f);
				const Rml::Colourb Colour = Tone < 0.0f ? Lerp(Mid, Low, -Tone) : Lerp(Mid, High, Tone);
				return Colour.ToPremultiplied(Opacity);
			};

			const int Count = int(Values.size());
			const int Slots = std::max(Length, Count);
			std::vector<float> Fractions(Count);
			std::vector<float> Heights(Count);
			std::vector<float> PointTones(Count);
			for (int Index = 0; Index < Count; ++Index)
			{
				const int Slot = Slots - Count + Index;
				Fractions[Index] = Slots > 1 ? float(Slot) / float(Slots - 1) : 1.0f;
				Heights[Index] = Rml::Math::Clamp(Normalise(Values[Index]), 0.0f, 1.0f);
				PointTones[Index] = Index < int(Tones.size()) ? Tones[Index] : 0.0f;
			}

			std::vector<Rml::Vector2f> Line;
			std::vector<Rml::ColourbPremultiplied> LineColours;
			if (Curve == CurveSmooth && Count > 2)
			{
				const std::vector<float> Tangents = MonotoneTangents(Heights);
				const float SegmentWidth = (Size.x - 2.0f * Inset) / float(std::max(Slots - 1, 1));
				const int Steps = Rml::Math::Clamp(int(std::ceil(SegmentWidth / 3.0f)), 1, 16);
				for (int Index = 0; Index + 1 < Count; ++Index)
				{
					for (int Step = 0; Step < Steps; ++Step)
					{
						const float T = float(Step) / float(Steps);
						const float T2 = T * T;
						const float T3 = T2 * T;
						const float Height = (2.0f * T3 - 3.0f * T2 + 1.0f) * Heights[Index] + (T3 - 2.0f * T2 + T) * Tangents[Index]
							+ (-2.0f * T3 + 3.0f * T2) * Heights[Index + 1] + (T3 - T2) * Tangents[Index + 1];
						Line.push_back(Place(Fractions[Index] + (Fractions[Index + 1] - Fractions[Index]) * T, Height));
						LineColours.push_back(ToneColour(PointTones[Index] + (PointTones[Index + 1] - PointTones[Index]) * T));
					}
				}
				Line.push_back(Place(Fractions.back(), Heights.back()));
				LineColours.push_back(ToneColour(PointTones.back()));
			}
			else
			{
				for (int Index = 0; Index < Count; ++Index)
				{
					Line.push_back(Place(Fractions[Index], Heights[Index]));
					LineColours.push_back(ToneColour(PointTones[Index]));
					// A step holds each value until the next one, then rises or drops straight to it.
					if (Curve == CurveStep && Index + 1 < Count && Heights[Index + 1] != Heights[Index])
					{
						Line.push_back(Place(Fractions[Index + 1], Heights[Index]));
						LineColours.push_back(LineColours.back());
					}
				}
			}

			if (FillOpacity > 0.0f && Line.size() > 1)
			{
				const float Bottom = bBaseline ? Place(0.0f, Normalise(Baseline)).y : Size.y;
				const FFillArea Area{ Line, LineColours, Bottom, FillOpacity, FillFade };
				const float StripeHalfWidth = FillWidth * 0.5f;
				switch (FillPattern)
				{
				case PatternHatch:
					AddStripes(Mesh, Area, Rml::Vector2f(0.7071f, -0.7071f), FillSpacing, StripeHalfWidth);
					break;
				case PatternCrosshatch:
					AddStripes(Mesh, Area, Rml::Vector2f(0.7071f, -0.7071f), FillSpacing, StripeHalfWidth);
					AddStripes(Mesh, Area, Rml::Vector2f(0.7071f, 0.7071f), FillSpacing, StripeHalfWidth);
					break;
				case PatternColumns:
					AddStripes(Mesh, Area, Rml::Vector2f(0.0f, 1.0f), FillSpacing, StripeHalfWidth);
					break;
				case PatternRows:
					AddStripes(Mesh, Area, Rml::Vector2f(1.0f, 0.0f), FillSpacing, StripeHalfWidth);
					break;
				case PatternDots:
					AddDots(Mesh, Area, FillSpacing, std::max(FillWidth * 0.5f, 0.5f));
					break;
				default:
					AddSolidFill(Mesh, Area);
					break;
				}
			}

			if (bAverage)
			{
				const float Height = Normalise(Average);
				const Rml::Colourb Colour = GetColour(Ids.AverageColour, Rml::Colourb(255, 255, 255, 90));
				const std::vector<Rml::Vector2f> AverageLine = { Place(0.0f, Height), Place(1.0f, Height) };
				const std::vector<Rml::ColourbPremultiplied> Colours(2, Colour.ToPremultiplied(Opacity));
				AddStrip(Mesh, AverageLine, Colours, 0.5f);
			}

			if (Count == 0)
			{
				return;
			}
			if (Line.size() > 1 && HalfWidth > 0.0f)
			{
				AddStrip(Mesh, Line, LineColours, HalfWidth);
			}
			if (PointRadius > 0.0f)
			{
				for (int Index = 0; Index < Count; ++Index)
				{
					AddDot(Mesh, Place(Fractions[Index], Heights[Index]), PointRadius, ToneColour(PointTones[Index]));
				}
			}
			if (Radius > 0.0f)
			{
				AddDot(Mesh, Line.back(), Radius, LineColours.back());
			}
		}
	};

	/// Adds `<line-graph>` and its `graph-*` properties. Call after Rml::Initialise, as with the decorators.
	inline void RegisterFolioLineGraph()
	{
		Detail::FLineGraphProperties& Ids = Detail::LineGraphProperties();
		if (Ids.bRegistered)
		{
			return;
		}
		Ids.bRegistered = true;

		Ids.LowColour = Rml::StyleSheetSpecification::RegisterProperty("graph-low-color", "#e5534b", false, false)
			.AddParser("color")
			.GetId();
		Ids.MidColour = Rml::StyleSheetSpecification::RegisterProperty("graph-mid-color", "#4ea1f3", false, false)
			.AddParser("color")
			.GetId();
		Ids.HighColour = Rml::StyleSheetSpecification::RegisterProperty("graph-high-color", "#57d38c", false, false)
			.AddParser("color")
			.GetId();
		Ids.AverageColour = Rml::StyleSheetSpecification::RegisterProperty("graph-average-color", "#ffffff5a", false, false)
			.AddParser("color")
			.GetId();
		Ids.LineWidth = Rml::StyleSheetSpecification::RegisterProperty("graph-line-width", "2dp", false, false)
			.AddParser("length")
			.GetId();
		Ids.DotSize = Rml::StyleSheetSpecification::RegisterProperty("graph-dot-size", "6dp", false, false)
			.AddParser("length")
			.GetId();
		Ids.PointSize = Rml::StyleSheetSpecification::RegisterProperty("graph-point-size", "0dp", false, false)
			.AddParser("length")
			.GetId();
		Ids.FillOpacity = Rml::StyleSheetSpecification::RegisterProperty("graph-fill-opacity", "0", false, false)
			.AddParser("number")
			.GetId();
		Ids.FillFade = Rml::StyleSheetSpecification::RegisterProperty("graph-fill-fade", "0", false, false)
			.AddParser("number")
			.GetId();
		Ids.FillPattern = Rml::StyleSheetSpecification::RegisterProperty("graph-fill-pattern", "solid", false, false)
			.AddParser("keyword", "solid, hatch, crosshatch, columns, rows, dots")
			.GetId();
		Ids.FillSpacing = Rml::StyleSheetSpecification::RegisterProperty("graph-fill-spacing", "6dp", false, false)
			.AddParser("length")
			.GetId();
		Ids.FillWidth = Rml::StyleSheetSpecification::RegisterProperty("graph-fill-width", "1dp", false, false)
			.AddParser("length")
			.GetId();
		Ids.Curve = Rml::StyleSheetSpecification::RegisterProperty("graph-curve", "linear", false, false)
			.AddParser("keyword", "linear, smooth, step")
			.GetId();

		auto Instancer = std::make_unique<Rml::ElementInstancerGeneric<FElementLineGraph>>();
		Rml::Factory::RegisterElementInstancer("line-graph", Instancer.get());
		Detail::LineGraphInstancers().push_back(std::move(Instancer));
	}

	/// Drops the instancer. RmlUi holds a raw pointer, so this must happen after shutdown.
	inline void ReleaseFolioLineGraph()
	{
		Detail::LineGraphInstancers().clear();
		Detail::LineGraphProperties().bRegistered = false;
	}
}

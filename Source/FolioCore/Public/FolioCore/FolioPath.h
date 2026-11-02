#pragma once

#include "FolioCore/FolioCoreAPI.h"
#include "FolioCore/FolioTrace.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataTypeRegister.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace Folio
{
	namespace Detail
	{
		struct FPathProperties
		{
			bool bRegistered = false;
			Rml::PropertyId Fill = Rml::PropertyId::Invalid;
			Rml::PropertyId Width = Rml::PropertyId::Invalid;
			Rml::PropertyId Join = Rml::PropertyId::Invalid;
			Rml::PropertyId Cap = Rml::PropertyId::Invalid;
			Rml::PropertyId Dash = Rml::PropertyId::Invalid;
			Rml::PropertyId DashOffset = Rml::PropertyId::Invalid;
		};

		inline FPathProperties& PathProperties()
		{
			static FPathProperties Properties;
			return Properties;
		}

		inline std::vector<std::unique_ptr<Rml::ElementInstancer>>& PathInstancers()
		{
			static std::vector<std::unique_ptr<Rml::ElementInstancer>> Instancers;
			return Instancers;
		}

		/// One subpath of a parsed `d`, as cubic segments in user units. Lines and quadratics are stored as cubics
		/// too, so flattening has one case.
		struct FPathContour
		{
			Rml::Vector2f Start;
			/// Three points per segment: two controls and the end.
			std::vector<Rml::Vector2f> Cubics;
			bool bClosed = false;
		};

		/// SVG path data (`M L H V C S Q T A Z`, either case) parsed to absolute cubics. Parsing stops at the first
		/// thing it cannot read, keeping what came before it, as SVG does; Error is then the offending position.
		class FPathParser
		{
		public:
			std::vector<FPathContour> Contours;
			int Error = -1;

			void Parse(const Rml::String& Text)
			{
				FOLIO_TRACE_SCOPE("Folio/Path/Parse");

				Contours.clear();
				Error = -1;
				Begin = Text.c_str();
				Cursor = Begin;
				Current = Rml::Vector2f(0.0f);
				Control = Current;
				char Command = 0;
				char Previous = 0;
				while (true)
				{
					SkipSeparators();
					if (*Cursor == '\0')
					{
						return;
					}
					if (std::isalpha(static_cast<unsigned char>(*Cursor)))
					{
						if (std::strchr("MmLlHhVvCcSsQqTtAaZz", *Cursor) == nullptr)
						{
							Fail();
							return;
						}
						Command = *Cursor++;
					}
					else if (Command == 0 || Command == 'Z' || Command == 'z')
					{
						Fail();
						return;
					}
					if (!Run(Command, Previous))
					{
						Fail();
						return;
					}
					Previous = Command;
					// Further coordinates after a move are lines, per the spec.
					if (Command == 'M')
					{
						Command = 'L';
					}
					else if (Command == 'm')
					{
						Command = 'l';
					}
				}
			}

		private:
			const char* Begin = nullptr;
			const char* Cursor = nullptr;
			Rml::Vector2f Current;
			/// The last control point, reflected by S and T.
			Rml::Vector2f Control;

			void Fail()
			{
				Error = int(Cursor - Begin);
			}

			void SkipSeparators()
			{
				while (*Cursor == ' ' || *Cursor == ',' || *Cursor == '\t' || *Cursor == '\n' || *Cursor == '\r')
				{
					++Cursor;
				}
			}

			bool Number(float& Out)
			{
				SkipSeparators();
				char* End = nullptr;
				Out = std::strtof(Cursor, &End);
				if (End == Cursor)
				{
					return false;
				}
				Cursor = End;
				return true;
			}

			/// Arc flags are one character each and may run together with what follows (`a5 5 0 1110 10`).
			bool Flag(bool& Out)
			{
				SkipSeparators();
				if (*Cursor != '0' && *Cursor != '1')
				{
					return false;
				}
				Out = *Cursor++ == '1';
				return true;
			}

			bool Point(bool bRelative, Rml::Vector2f& Out)
			{
				float X = 0.0f;
				float Y = 0.0f;
				if (!Number(X) || !Number(Y))
				{
					return false;
				}
				Out = bRelative ? Current + Rml::Vector2f(X, Y) : Rml::Vector2f(X, Y);
				return true;
			}

			FPathContour& Contour()
			{
				if (Contours.empty() || Contours.back().bClosed)
				{
					// Drawing on after a Z starts a new subpath at the closed one's start.
					const Rml::Vector2f Start = Contours.empty() ? Current : Contours.back().Start;
					Contours.push_back(FPathContour{ Start, {}, false });
				}
				return Contours.back();
			}

			void Cubic(Rml::Vector2f A, Rml::Vector2f B, Rml::Vector2f End)
			{
				std::vector<Rml::Vector2f>& Out = Contour().Cubics;
				Out.push_back(A);
				Out.push_back(B);
				Out.push_back(End);
				Current = End;
			}

			void Line(Rml::Vector2f End)
			{
				const Rml::Vector2f From = Current;
				Cubic(From + (End - From) / 3.0f, From + (End - From) * (2.0f / 3.0f), End);
				Control = End;
			}

			void Quadratic(Rml::Vector2f Via, Rml::Vector2f End)
			{
				const Rml::Vector2f From = Current;
				Cubic(From + (Via - From) * (2.0f / 3.0f), End + (Via - End) * (2.0f / 3.0f), End);
				Control = Via;
			}

			/// Endpoint to centre parameterisation (SVG 1.1 appendix F.6.5), then one cubic per quarter turn or less.
			void Arc(Rml::Vector2f Radii, float Rotation, bool bLarge, bool bSweep, Rml::Vector2f End)
			{
				const Rml::Vector2f From = Current;
				float Rx = std::abs(Radii.x);
				float Ry = std::abs(Radii.y);
				if (Rx < 1e-6f || Ry < 1e-6f || (End - From).SquaredMagnitude() < 1e-12f)
				{
					if ((End - From).SquaredMagnitude() >= 1e-12f)
					{
						Line(End);
					}
					return;
				}

				const float Phi = Rotation * Rml::Math::RMLUI_PI / 180.0f;
				const float CosPhi = std::cos(Phi);
				const float SinPhi = std::sin(Phi);
				const Rml::Vector2f Half = (From - End) * 0.5f;
				const Rml::Vector2f P(CosPhi * Half.x + SinPhi * Half.y, -SinPhi * Half.x + CosPhi * Half.y);

				// Radii too small to reach the end are scaled up until they just do.
				const float Lambda = (P.x * P.x) / (Rx * Rx) + (P.y * P.y) / (Ry * Ry);
				if (Lambda > 1.0f)
				{
					Rx *= std::sqrt(Lambda);
					Ry *= std::sqrt(Lambda);
				}

				const float Numerator = Rx * Rx * Ry * Ry - Rx * Rx * P.y * P.y - Ry * Ry * P.x * P.x;
				const float Denominator = Rx * Rx * P.y * P.y + Ry * Ry * P.x * P.x;
				float Root = Denominator > 0.0f ? std::sqrt(std::max(Numerator / Denominator, 0.0f)) : 0.0f;
				if (bLarge == bSweep)
				{
					Root = -Root;
				}
				const Rml::Vector2f CentreP(Root * Rx * P.y / Ry, -Root * Ry * P.x / Rx);
				const Rml::Vector2f Centre(CosPhi * CentreP.x - SinPhi * CentreP.y + (From.x + End.x) * 0.5f,
					SinPhi * CentreP.x + CosPhi * CentreP.y + (From.y + End.y) * 0.5f);

				const auto Angle = [](Rml::Vector2f U, Rml::Vector2f V)
				{
					return std::atan2(U.x * V.y - U.y * V.x, U.x * V.x + U.y * V.y);
				};
				const Rml::Vector2f StartVector((P.x - CentreP.x) / Rx, (P.y - CentreP.y) / Ry);
				const Rml::Vector2f EndVector((-P.x - CentreP.x) / Rx, (-P.y - CentreP.y) / Ry);
				const float Theta = Angle(Rml::Vector2f(1.0f, 0.0f), StartVector);
				float Delta = Angle(StartVector, EndVector);
				if (!bSweep && Delta > 0.0f)
				{
					Delta -= 2.0f * Rml::Math::RMLUI_PI;
				}
				else if (bSweep && Delta < 0.0f)
				{
					Delta += 2.0f * Rml::Math::RMLUI_PI;
				}

				const auto OnEllipse = [&](float T)
				{
					const float X = Rx * std::cos(T);
					const float Y = Ry * std::sin(T);
					return Rml::Vector2f(CosPhi * X - SinPhi * Y + Centre.x, SinPhi * X + CosPhi * Y + Centre.y);
				};
				const auto Tangent = [&](float T)
				{
					const float X = -Rx * std::sin(T);
					const float Y = Ry * std::cos(T);
					return Rml::Vector2f(CosPhi * X - SinPhi * Y, SinPhi * X + CosPhi * Y);
				};

				const int Pieces = std::max(1, int(std::ceil(std::abs(Delta) / (Rml::Math::RMLUI_PI * 0.5f) - 1e-4f)));
				const float Step = Delta / float(Pieces);
				const float Kappa = 4.0f / 3.0f * std::tan(Step * 0.25f);
				for (int Piece = 0; Piece < Pieces; ++Piece)
				{
					const float T0 = Theta + Step * float(Piece);
					const float T1 = T0 + Step;
					const Rml::Vector2f A = OnEllipse(T0);
					const Rml::Vector2f B = Piece + 1 == Pieces ? End : OnEllipse(T1);
					Cubic(A + Tangent(T0) * Kappa, B - Tangent(T1) * Kappa, B);
				}
				Current = End;
				Control = End;
			}

			bool Run(char Command, char Previous)
			{
				const bool bRelative = std::islower(static_cast<unsigned char>(Command)) != 0;
				const char Upper = char(std::toupper(static_cast<unsigned char>(Command)));
				const char PreviousUpper = char(std::toupper(static_cast<unsigned char>(Previous)));
				Rml::Vector2f A;
				Rml::Vector2f B;
				Rml::Vector2f End;
				float Value = 0.0f;
				switch (Upper)
				{
				case 'M':
					if (!Point(bRelative, End))
					{
						return false;
					}
					Contours.push_back(FPathContour{ End, {}, false });
					Current = End;
					Control = End;
					return true;
				case 'L':
					if (!Point(bRelative, End))
					{
						return false;
					}
					Line(End);
					return true;
				case 'H':
					if (!Number(Value))
					{
						return false;
					}
					Line(Rml::Vector2f(bRelative ? Current.x + Value : Value, Current.y));
					return true;
				case 'V':
					if (!Number(Value))
					{
						return false;
					}
					Line(Rml::Vector2f(Current.x, bRelative ? Current.y + Value : Value));
					return true;
				case 'C':
					if (!Point(bRelative, A) || !Point(bRelative, B) || !Point(bRelative, End))
					{
						return false;
					}
					Cubic(A, B, End);
					Control = B;
					return true;
				case 'S':
					if (!Point(bRelative, B) || !Point(bRelative, End))
					{
						return false;
					}
					A = PreviousUpper == 'C' || PreviousUpper == 'S' ? Current * 2.0f - Control : Current;
					Cubic(A, B, End);
					Control = B;
					return true;
				case 'Q':
					if (!Point(bRelative, A) || !Point(bRelative, End))
					{
						return false;
					}
					Quadratic(A, End);
					return true;
				case 'T':
					if (!Point(bRelative, End))
					{
						return false;
					}
					A = PreviousUpper == 'Q' || PreviousUpper == 'T' ? Current * 2.0f - Control : Current;
					Quadratic(A, End);
					return true;
				case 'A':
				{
					float Rx = 0.0f;
					float Ry = 0.0f;
					float Rotation = 0.0f;
					bool bLarge = false;
					bool bSweep = false;
					if (!Number(Rx) || !Number(Ry) || !Number(Rotation) || !Flag(bLarge) || !Flag(bSweep)
						|| !Point(bRelative, End))
					{
						return false;
					}
					Arc(Rml::Vector2f(Rx, Ry), Rotation, bLarge, bSweep, End);
					return true;
				}
				case 'Z':
					if (!Contours.empty() && !Contours.back().bClosed)
					{
						Contours.back().bClosed = true;
						Current = Contours.back().Start;
						Control = Current;
					}
					return true;
				default:
					return false;
				}
			}
		};

		/// The area of every contour, filled by the nonzero or even-odd rule, with a feather of Feather pixels outside its
		/// edge. Open contours are closed for the fill, as SVG does.
		///
		/// The plane is cut into horizontal slabs at every vertex and every crossing, so no two edges cross inside a
		/// slab and each slab's filled spans are trapezoids. Overlaps, holes and self-intersection need nothing more.
		inline void AddFill(Rml::Mesh& Mesh, const std::vector<std::vector<Rml::Vector2f>>& Contours,
			Rml::ColourbPremultiplied Solid, bool bEvenOdd, float Feather)
		{
			FOLIO_TRACE_SCOPE("Folio/Path/Fill");

			struct FEdge
			{
				Rml::Vector2f Top;
				Rml::Vector2f Bottom;
				int Winding;

				float XAt(float Y) const
				{
					const float Span = Bottom.y - Top.y;
					return Span > 0.0f ? Top.x + (Bottom.x - Top.x) * ((Y - Top.y) / Span) : Top.x;
				}
			};
			std::vector<FEdge> Edges;
			std::vector<float> Breaks;
			for (const std::vector<Rml::Vector2f>& Points : Contours)
			{
				const int Count = int(Points.size());
				if (Count < 3)
				{
					continue;
				}
				for (int Index = 0; Index < Count; ++Index)
				{
					const Rml::Vector2f A = Points[Index];
					const Rml::Vector2f B = Points[(Index + 1) % Count];
					Breaks.push_back(A.y);
					if (std::abs(A.y - B.y) < 1e-6f)
					{
						continue;
					}
					Edges.push_back(A.y < B.y ? FEdge{ A, B, 1 } : FEdge{ B, A, -1 });
				}
			}
			if (Edges.empty())
			{
				return;
			}
			std::sort(Edges.begin(), Edges.end(), [](const FEdge& A, const FEdge& B) { return A.Top.y < B.Top.y; });

			for (size_t First = 0; First < Edges.size(); ++First)
			{
				const FEdge& A = Edges[First];
				for (size_t Second = First + 1; Second < Edges.size() && Edges[Second].Top.y < A.Bottom.y; ++Second)
				{
					const FEdge& B = Edges[Second];
					const float Upper = std::max(A.Top.y, B.Top.y);
					const float Lower = std::min(A.Bottom.y, B.Bottom.y);
					if (Lower <= Upper)
					{
						continue;
					}
					const float DifferenceUpper = A.XAt(Upper) - B.XAt(Upper);
					const float DifferenceLower = A.XAt(Lower) - B.XAt(Lower);
					if ((DifferenceUpper < 0.0f && DifferenceLower > 0.0f) || (DifferenceUpper > 0.0f && DifferenceLower < 0.0f))
					{
						Breaks.push_back(Upper + (Lower - Upper) * (DifferenceUpper / (DifferenceUpper - DifferenceLower)));
					}
				}
			}
			std::sort(Breaks.begin(), Breaks.end());
			Breaks.erase(std::unique(Breaks.begin(), Breaks.end(), [](float A, float B) { return B - A < 1e-3f; }), Breaks.end());

			const Rml::ColourbPremultiplied Clear(0, 0, 0, 0);
			const auto Vertex = [&Mesh](Rml::Vector2f Position, Rml::ColourbPremultiplied Colour)
			{
				Rml::Vertex Out;
				Out.position = Position;
				Out.colour = Colour;
				Out.tex_coord = Rml::Vector2f(0.0f, 0.0f);
				Mesh.vertices.push_back(Out);
				return int(Mesh.vertices.size()) - 1;
			};
			const auto Quad = [&Mesh](int A, int B, int C, int D) { Mesh.indices.insert(Mesh.indices.end(), { A, B, C, A, C, D }); };

			// A feather outward from the edge A to B, on the side whose x has the sign of Side.
			const auto SideFeather = [&](Rml::Vector2f A, Rml::Vector2f B, float Side)
			{
				const Rml::Vector2f Direction = (B - A).Normalise();
				Rml::Vector2f Normal(-Direction.y, Direction.x);
				if (Normal.x * Side < 0.0f)
				{
					Normal = -Normal;
				}
				Quad(Vertex(A, Solid), Vertex(B, Solid), Vertex(B + Normal * Feather, Clear), Vertex(A + Normal * Feather, Clear));
			};
			// A feather up (Direction -1) or down (+1) from a horizontal stretch of edge, rounding off its ends.
			const auto FlatFeather = [&](float Left, float Right, float Y, float Direction)
			{
				const float Out = Y + Direction * Feather;
				const int A = Vertex(Rml::Vector2f(Left, Y), Solid);
				const int B = Vertex(Rml::Vector2f(Right, Y), Solid);
				Quad(A, B, Vertex(Rml::Vector2f(Right, Out), Clear), Vertex(Rml::Vector2f(Left, Out), Clear));
				Quad(A, Vertex(Rml::Vector2f(Left, Out), Clear), Vertex(Rml::Vector2f(Left - Feather, Out), Clear),
					Vertex(Rml::Vector2f(Left - Feather, Y), Clear));
				Quad(B, Vertex(Rml::Vector2f(Right + Feather, Y), Clear), Vertex(Rml::Vector2f(Right + Feather, Out), Clear),
					Vertex(Rml::Vector2f(Right, Out), Clear));
			};

			using FSpans = std::vector<std::pair<float, float>>;
			// The stretches of A not covered by B, both sorted and disjoint.
			const auto Subtract = [](const FSpans& A, const FSpans& B)
			{
				FSpans Out;
				size_t Cover = 0;
				for (const std::pair<float, float>& Span : A)
				{
					float From = Span.first;
					while (Cover < B.size() && B[Cover].second <= From)
					{
						++Cover;
					}
					for (size_t Index = Cover; Index < B.size() && B[Index].first < Span.second; ++Index)
					{
						if (B[Index].first > From)
						{
							Out.emplace_back(From, B[Index].first);
						}
						From = std::max(From, B[Index].second);
					}
					if (Span.second > From)
					{
						Out.emplace_back(From, Span.second);
					}
				}
				Out.erase(std::remove_if(Out.begin(), Out.end(),
					[](const std::pair<float, float>& Span) { return Span.second - Span.first < 1e-3f; }), Out.end());
				return Out;
			};

			struct FActive
			{
				const FEdge* Edge;
				float Upper;
				float Lower;
			};
			std::vector<FActive> Active;
			FSpans Above;
			for (size_t Slab = 0; Slab + 1 < Breaks.size(); ++Slab)
			{
				const float Y0 = Breaks[Slab];
				const float Y1 = Breaks[Slab + 1];
				Active.clear();
				for (const FEdge& Edge : Edges)
				{
					if (Edge.Top.y > Y0 + 1e-4f)
					{
						break;
					}
					if (Edge.Bottom.y >= Y1 - 1e-4f)
					{
						Active.push_back(FActive{ &Edge, Edge.XAt(Y0), Edge.XAt(Y1) });
					}
				}
				std::sort(Active.begin(), Active.end(), [](const FActive& A, const FActive& B)
				{
					return A.Upper + A.Lower < B.Upper + B.Lower;
				});

				FSpans Top;
				FSpans Bottom;
				int Winding = 0;
				const FActive* Start = nullptr;
				for (const FActive& Edge : Active)
				{
					const bool bWasInside = bEvenOdd ? (Winding & 1) != 0 : Winding != 0;
					Winding += Edge.Edge->Winding;
					const bool bInside = bEvenOdd ? (Winding & 1) != 0 : Winding != 0;
					if (!bWasInside && bInside)
					{
						Start = &Edge;
					}
					else if (bWasInside && !bInside && Start != nullptr)
					{
						// Edges that coincide enclose nothing, and their feathers alone would draw a line.
						if (Edge.Upper - Start->Upper < 1e-3f && Edge.Lower - Start->Lower < 1e-3f)
						{
							continue;
						}
						const Rml::Vector2f A(Start->Upper, Y0);
						const Rml::Vector2f B(Edge.Upper, Y0);
						const Rml::Vector2f C(Edge.Lower, Y1);
						const Rml::Vector2f D(Start->Lower, Y1);
						Quad(Vertex(A, Solid), Vertex(B, Solid), Vertex(C, Solid), Vertex(D, Solid));
						SideFeather(A, D, -1.0f);
						SideFeather(B, C, 1.0f);
						Top.emplace_back(A.x, B.x);
						Bottom.emplace_back(D.x, C.x);
					}
				}
				// Where this slab starts filling or the one above stops, the edge is horizontal.
				for (const std::pair<float, float>& Span : Subtract(Top, Above))
				{
					FlatFeather(Span.first, Span.second, Y0, -1.0f);
				}
				for (const std::pair<float, float>& Span : Subtract(Above, Top))
				{
					FlatFeather(Span.first, Span.second, Y0, 1.0f);
				}
				Above = std::move(Bottom);
			}
			for (const std::pair<float, float>& Span : Above)
			{
				FlatFeather(Span.first, Span.second, Breaks.back(), 1.0f);
			}
		}
	}

	/// `<path>`: an SVG path drawn as a stroke, one mesh and one draw for the whole path.
	///
	/// Attributes, bindable with `data-attr-*`:
	///  - `d`: SVG path data. Every command is supported, arcs included.
	///  - `viewbox`: `min-x min-y width height`, the user space fitted into the content box, centred and uniformly
	///    scaled as SVG's default `xMidYMid meet`. Without it a user unit is 1dp from the content box's corner.
	///
	/// The stroke is drawn in `color`. Properties: `path-width`, `path-join` (`miter`, `round`, `bevel`) and `path-cap`
	/// (`butt`, `round`, `square`). The width is a length and is not scaled by the view box. A mitre longer than four
	/// times the width bevels, SVG's default limit.
	///
	/// RmlUi allows 128 element properties in all and Folio has nearly used them, so each new `path-*` property has
	/// to earn its slot.
	class FElementPath : public Rml::Element
	{
	public:
		explicit FElementPath(const Rml::String& Tag)
			: Rml::Element(Tag)
		{
		}

	protected:
		void OnAttributeChange(const Rml::ElementAttributes& Changed) override
		{
			Rml::Element::OnAttributeChange(Changed);

			if (Changed.find("d") != Changed.end())
			{
				const Rml::String Text = GetAttribute<Rml::String>("d", "");
				Parser.Parse(Text);
				if (Parser.Error >= 0)
				{
					Rml::Log::Message(Rml::Log::LT_WARNING, "<path> d is unreadable at character %d: '%s'. Drawing what came before it.",
						Parser.Error, Text.substr(size_t(Parser.Error), 24).c_str());
				}
				bDirty = true;
			}
			if (Changed.find("fill-rule") != Changed.end())
			{
				const Rml::String Rule = GetAttribute<Rml::String>("fill-rule", "nonzero");
				bEvenOdd = Rule == "evenodd";
				if (!bEvenOdd && Rule != "nonzero")
				{
					Rml::Log::Message(Rml::Log::LT_WARNING, "<path> fill-rule '%s' is neither nonzero nor evenodd.", Rule.c_str());
				}
				bDirty = true;
			}
			if (Changed.find("viewbox") != Changed.end())
			{
				std::vector<float> Numbers;
				const Rml::String Text = GetAttribute<Rml::String>("viewbox", "");
				const char* Cursor = Text.c_str();
				while (true)
				{
					while (*Cursor == ' ' || *Cursor == ',')
					{
						++Cursor;
					}
					char* End = nullptr;
					const float Value = std::strtof(Cursor, &End);
					if (End == Cursor)
					{
						break;
					}
					Numbers.push_back(Value);
					Cursor = End;
				}
				bViewBox = Numbers.size() == 4 && Numbers[2] > 0.0f && Numbers[3] > 0.0f;
				if (bViewBox)
				{
					ViewBox = { Numbers[0], Numbers[1], Numbers[2], Numbers[3] };
				}
				else if (!Text.empty())
				{
					Rml::Log::Message(Rml::Log::LT_WARNING,
						"<path> viewbox '%s' needs four numbers, min-x min-y width height, with a positive size.", Text.c_str());
				}
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
		/// Keyword indices, in the order RegisterFolioPath lists them.
		enum EJoin { JoinMiter, JoinRound, JoinBevel };
		enum ECap { CapButt, CapRound, CapSquare };

		Detail::FPathParser Parser;
		struct FViewBox
		{
			float X = 0.0f;
			float Y = 0.0f;
			float Width = 0.0f;
			float Height = 0.0f;
		} ViewBox;
		bool bViewBox = false;
		bool bEvenOdd = false;
		bool bDirty = true;
		Rml::Geometry Drawn;
		std::vector<std::vector<Rml::Vector2f>> Flattened;
		float HitHalfWidth = 0.0f;
		bool bHitFill = false;

		/// Coverage fades to nothing over this many pixels past each edge, as `<line-graph>` does.
		static constexpr float Feather = 1.0f;
		/// How far a flattened curve may stray from the true one, in pixels.
		static constexpr float Tolerance = 0.2f;

		/// Everything one contour's stroke needs, so the geometry helpers take one argument.
		struct FStroke
		{
			Rml::Mesh& Mesh;
			float HalfWidth;
			Rml::ColourbPremultiplied Solid;
			int Join;
			int Cap;
			float MiterLimit;
		};

		static float Cross(Rml::Vector2f A, Rml::Vector2f B)
		{
			return A.x * B.y - A.y * B.x;
		}

		static float Dot(Rml::Vector2f A, Rml::Vector2f B)
		{
			return A.x * B.x + A.y * B.y;
		}

		static Rml::Vector2f Normal(Rml::Vector2f Direction)
		{
			return Rml::Vector2f(-Direction.y, Direction.x);
		}

		static int AddVertex(Rml::Mesh& Mesh, Rml::Vector2f Position, Rml::ColourbPremultiplied Colour)
		{
			Rml::Vertex Vertex;
			Vertex.position = Position;
			Vertex.colour = Colour;
			Vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
			Mesh.vertices.push_back(Vertex);
			return int(Mesh.vertices.size()) - 1;
		}

		static void AddTriangle(Rml::Mesh& Mesh, int A, int B, int C)
		{
			Mesh.indices.insert(Mesh.indices.end(), { A, B, C });
		}

		static void AddQuad(Rml::Mesh& Mesh, int A, int B, int C, int D)
		{
			Mesh.indices.insert(Mesh.indices.end(), { A, B, C, A, C, D });
		}

		/// Where a stroke crosses a point: the solid core either side and the transparent feather beyond it.
		/// Left is along +Normal of the direction of travel.
		struct FSection
		{
			int LeftEdge;
			int LeftCore;
			int RightCore;
			int RightEdge;
		};

		static FSection AddSection(const FStroke& Stroke, Rml::Vector2f Left, Rml::Vector2f LeftEdge, Rml::Vector2f Right,
			Rml::Vector2f RightEdge)
		{
			const Rml::ColourbPremultiplied Clear(0, 0, 0, 0);
			FSection Section;
			Section.LeftEdge = AddVertex(Stroke.Mesh, LeftEdge, Clear);
			Section.LeftCore = AddVertex(Stroke.Mesh, Left, Stroke.Solid);
			Section.RightCore = AddVertex(Stroke.Mesh, Right, Stroke.Solid);
			Section.RightEdge = AddVertex(Stroke.Mesh, RightEdge, Clear);
			return Section;
		}

		static void Connect(const FStroke& Stroke, const FSection& From, const FSection& To)
		{
			AddQuad(Stroke.Mesh, From.LeftEdge, To.LeftEdge, To.LeftCore, From.LeftCore);
			AddQuad(Stroke.Mesh, From.LeftCore, To.LeftCore, To.RightCore, From.RightCore);
			AddQuad(Stroke.Mesh, From.RightCore, To.RightCore, To.RightEdge, From.RightEdge);
		}

		/// The outer wedge of a bevelled or round join, or a round cap: a fan from Pivot over the arc of radius
		/// HalfWidth around Centre from Start to End, with its feather. Start and End are unit normals; the arc turns
		/// the short way unless bHalfTurn.
		static void AddWedge(const FStroke& Stroke, int Pivot, Rml::Vector2f Centre, Rml::Vector2f Start, Rml::Vector2f End,
			bool bRound, bool bHalfTurn, float HalfTurnSign)
		{
			const Rml::ColourbPremultiplied Clear(0, 0, 0, 0);
			float Sweep = bHalfTurn ? HalfTurnSign * Rml::Math::RMLUI_PI : std::atan2(Cross(Start, End), Dot(Start, End));
			int Steps = 1;
			if (bRound)
			{
				// Fine enough that each chord stays within the tolerance of the true circle.
				const float Radius = Stroke.HalfWidth + Feather;
				const float MaxStep = 2.0f * std::acos(Rml::Math::Clamp(1.0f - Tolerance / std::max(Radius, Tolerance), -1.0f, 1.0f));
				Steps = Rml::Math::Clamp(int(std::ceil(std::abs(Sweep) / std::max(MaxStep, 0.01f))), 1, 64);
			}
			const float StartAngle = std::atan2(Start.y, Start.x);
			int PreviousCore = -1;
			int PreviousEdge = -1;
			for (int Step = 0; Step <= Steps; ++Step)
			{
				Rml::Vector2f Direction;
				if (Step == 0)
				{
					Direction = Start;
				}
				else if (Step == Steps)
				{
					Direction = End;
				}
				else
				{
					const float Angle = StartAngle + Sweep * float(Step) / float(Steps);
					Direction = Rml::Vector2f(std::cos(Angle), std::sin(Angle));
				}
				const int Core = AddVertex(Stroke.Mesh, Centre + Direction * Stroke.HalfWidth, Stroke.Solid);
				const int Edge = AddVertex(Stroke.Mesh, Centre + Direction * (Stroke.HalfWidth + Feather), Clear);
				if (PreviousCore >= 0)
				{
					AddTriangle(Stroke.Mesh, Pivot, PreviousCore, Core);
					AddQuad(Stroke.Mesh, PreviousCore, PreviousEdge, Edge, Core);
				}
				PreviousCore = Core;
				PreviousEdge = Edge;
			}
		}

		/// A butt or square end: the section at Point, plus a feather off the end so the cut is antialiased too.
		/// Outward points away from the stroke. Returns the section the stroke's body connects to.
		static FSection AddFlatCap(const FStroke& Stroke, Rml::Vector2f Point, Rml::Vector2f Outward, bool bSquare)
		{
			const Rml::ColourbPremultiplied Clear(0, 0, 0, 0);
			const Rml::Vector2f End = bSquare ? Point + Outward * Stroke.HalfWidth : Point;
			// Left of travel when travelling outward.
			const Rml::Vector2f Side = Normal(Outward);
			const float W = Stroke.HalfWidth;
			const float F = W + Feather;
			const FSection Section = AddSection(Stroke, End + Side * W, End + Side * F, End - Side * W, End - Side * F);
			const int LeftEdge = AddVertex(Stroke.Mesh, End + Side * F + Outward * Feather, Clear);
			const int LeftCore = AddVertex(Stroke.Mesh, End + Side * W + Outward * Feather, Clear);
			const int RightCore = AddVertex(Stroke.Mesh, End - Side * W + Outward * Feather, Clear);
			const int RightEdge = AddVertex(Stroke.Mesh, End - Side * F + Outward * Feather, Clear);
			AddQuad(Stroke.Mesh, Section.LeftEdge, LeftEdge, LeftCore, Section.LeftCore);
			AddQuad(Stroke.Mesh, Section.LeftCore, LeftCore, RightCore, Section.RightCore);
			AddQuad(Stroke.Mesh, Section.RightCore, RightCore, RightEdge, Section.RightEdge);
			if (!bSquare)
			{
				return Section;
			}
			// A square cap's body runs back to the real end point, so give it a section there to connect to.
			const FSection Body = AddSection(Stroke, Point + Side * W, Point + Side * F, Point - Side * W, Point - Side * F);
			Connect(Stroke, Body, Section);
			return Body;
		}

		/// A section facing along Direction, for the start or end of an open contour. Caps are built facing outward,
		/// so a start cap's left is the body's right.
		static FSection Facing(const FSection& Cap, bool bStart)
		{
			return bStart ? FSection{ Cap.RightEdge, Cap.RightCore, Cap.LeftCore, Cap.LeftEdge } : Cap;
		}

		static FSection AddCap(const FStroke& Stroke, Rml::Vector2f Point, Rml::Vector2f Direction, bool bStart)
		{
			const Rml::Vector2f Outward = bStart ? -Direction : Direction;
			if (Stroke.Cap == CapRound)
			{
				const Rml::Vector2f N = Normal(Direction);
				const float W = Stroke.HalfWidth;
				const float F = W + Feather;
				const FSection Section = AddSection(Stroke, Point + N * W, Point + N * F, Point - N * W, Point - N * F);
				const int Pivot = AddVertex(Stroke.Mesh, Point, Stroke.Solid);
				// From one side, around the outward end, to the other.
				const Rml::Vector2f From = bStart ? -N : N;
				AddWedge(Stroke, Pivot, Point, From, -From, true, true, Cross(From, Outward) >= 0.0f ? 1.0f : -1.0f);
				return Section;
			}
			return Facing(AddFlatCap(Stroke, Point, Outward, Stroke.Cap == CapSquare), bStart);
		}

		/// The stroke along one flattened contour.
		static void AddContour(const FStroke& Stroke, std::vector<Rml::Vector2f> Points, bool bClosed)
		{
			// Coincident points have no direction, so they would break every join they touch.
			std::vector<Rml::Vector2f> Kept;
			Kept.reserve(Points.size());
			for (const Rml::Vector2f& Point : Points)
			{
				if (Kept.empty() || (Point - Kept.back()).SquaredMagnitude() > 1e-4f)
				{
					Kept.push_back(Point);
				}
			}
			if (bClosed && Kept.size() > 1 && (Kept.front() - Kept.back()).SquaredMagnitude() <= 1e-4f)
			{
				Kept.pop_back();
			}
			Points = std::move(Kept);
			const int Count = int(Points.size());
			if (Count == 0)
			{
				return;
			}
			if (Count == 1)
			{
				// A zero-length subpath draws its cap alone, as SVG does: a dot or a square, nothing for butt.
				if (Stroke.Cap == CapButt)
				{
					return;
				}
				const Rml::Vector2f Direction(1.0f, 0.0f);
				const FSection Start = AddCap(Stroke, Points[0], Direction, true);
				const FSection End = AddCap(Stroke, Points[0], Direction, false);
				if (Stroke.Cap == CapSquare)
				{
					Connect(Stroke, Start, End);
				}
				return;
			}
			if (Count == 2)
			{
				bClosed = false;
			}

			const auto DirectionAt = [&Points, Count](int From)
			{
				return (Points[(From + 1) % Count] - Points[From]).Normalise();
			};
			const auto LengthAt = [&Points, Count](int From)
			{
				return (Points[(From + 1) % Count] - Points[From]).Magnitude();
			};

			const int Segments = bClosed ? Count : Count - 1;
			// Each segment's start and end section; a mitred join shares one section between its two segments.
			std::vector<FSection> Starts(Segments);
			std::vector<FSection> Ends(Segments);

			const float W = Stroke.HalfWidth;
			const float F = W + Feather;
			const int FirstJoin = bClosed ? 0 : 1;
			const int LastJoin = bClosed ? Count - 1 : Count - 2;
			for (int Index = FirstJoin; Index <= LastJoin; ++Index)
			{
				const int In = (Index - 1 + Count) % Count;
				const int Out = Index;
				const Rml::Vector2f P = Points[Index];
				const Rml::Vector2f DirectionIn = DirectionAt(In);
				const Rml::Vector2f DirectionOut = DirectionAt(Out);
				const Rml::Vector2f NormalIn = Normal(DirectionIn);
				const Rml::Vector2f NormalOut = Normal(DirectionOut);

				// The mitre direction, scaled so its length is 1/cos(half the turn): the offset that keeps the
				// stroke's width across the corner.
				Rml::Vector2f Mitre = (NormalIn + NormalOut) * 0.5f;
				const float MitreSquared = Mitre.SquaredMagnitude();
				const bool bReversal = MitreSquared < 1e-6f;
				if (!bReversal)
				{
					Mitre = Mitre / MitreSquared;
				}
				const float MitreLength = bReversal ? FLT_MAX : std::sqrt(1.0f / MitreSquared);
				// Turns this slight are indistinguishable from any join, so flattened curves stay one continuous strip.
				const bool bSlight = MitreLength < 1.01f;
				const bool bMitre = !bReversal && (bSlight || (Stroke.Join == JoinMiter && MitreLength <= Stroke.MiterLimit));
				const int SegmentIn = bClosed ? In : Index - 1;
				const int SegmentOut = Index;
				const int SegmentOutSlot = SegmentOut < Segments ? SegmentOut : -1;

				if (bMitre)
				{
					const FSection Shared = AddSection(Stroke, P + Mitre * W, P + Mitre * F, P - Mitre * W, P - Mitre * F);
					Ends[SegmentIn] = Shared;
					if (SegmentOutSlot >= 0)
					{
						Starts[SegmentOutSlot] = Shared;
					}
					continue;
				}

				// Turning towards +Normal puts the outside of the corner on -Normal.
				const float Outside = Dot(NormalIn, DirectionOut) > 0.0f ? -1.0f : 1.0f;
				const Rml::Vector2f OuterIn = NormalIn * Outside;
				const Rml::Vector2f OuterOut = NormalOut * Outside;
				// The inner corner meets at the mitre point. Where that would reach past the end of either segment, as
				// at a hairpin or a short segment, each side keeps its own inner corner and the two overlap inside the
				// stroke instead.
				const Rml::Vector2f Inner = bReversal ? -NormalIn * Outside : -Mitre * Outside;
				const bool bInnerFits = Inner.Magnitude() * F <= std::min(LengthAt(In), LengthAt(Out));

				// Outside < 0 means the outer side is the right one.
				const auto Section = [&](Rml::Vector2f Outer)
				{
					const Rml::Vector2f InnerSide = bInnerFits ? Inner : -Outer;
					const Rml::Vector2f InnerCore = P + InnerSide * W;
					const Rml::Vector2f InnerEdge = P + InnerSide * F;
					return Outside < 0.0f
						? AddSection(Stroke, InnerCore, InnerEdge, P + Outer * W, P + Outer * F)
						: AddSection(Stroke, P + Outer * W, P + Outer * F, InnerCore, InnerEdge);
				};
				Ends[SegmentIn] = Section(OuterIn);
				if (SegmentOutSlot >= 0)
				{
					Starts[SegmentOutSlot] = Section(OuterOut);
				}
				const int Pivot = AddVertex(Stroke.Mesh, bInnerFits ? P + Inner * W : P, Stroke.Solid);
				AddWedge(Stroke, Pivot, P, OuterIn, OuterOut, Stroke.Join == JoinRound, bReversal,
					Cross(OuterIn, DirectionIn) >= 0.0f ? 1.0f : -1.0f);
			}

			if (!bClosed)
			{
				Starts[0] = AddCap(Stroke, Points[0], DirectionAt(0), true);
				Ends[Segments - 1] = AddCap(Stroke, Points[Count - 1], DirectionAt(Count - 2), false);
			}
			for (int Segment = 0; Segment < Segments; ++Segment)
			{
				Connect(Stroke, Starts[Segment], Ends[Segment]);
			}
		}

		/// Adaptive subdivision until the control points lie within Tolerance of the chord.
		static void Flatten(std::vector<Rml::Vector2f>& Out, Rml::Vector2f P0, Rml::Vector2f P1, Rml::Vector2f P2,
			Rml::Vector2f P3, int Depth)
		{
			const Rml::Vector2f Chord = P3 - P0;
			const float ChordSquared = Chord.SquaredMagnitude();
			bool bFlat = Depth >= 12;
			if (!bFlat && ChordSquared > 1e-8f)
			{
				const float Distance1 = std::abs(Cross(P1 - P3, Chord));
				const float Distance2 = std::abs(Cross(P2 - P3, Chord));
				bFlat = (Distance1 + Distance2) * (Distance1 + Distance2) <= Tolerance * Tolerance * ChordSquared;
			}
			else if (!bFlat)
			{
				// A curve that returns to where it started has no chord to measure against, only its controls.
				bFlat = (P1 - P0).SquaredMagnitude() + (P2 - P0).SquaredMagnitude() <= Tolerance * Tolerance;
			}
			if (bFlat)
			{
				Out.push_back(P3);
				return;
			}
			const Rml::Vector2f P01 = (P0 + P1) * 0.5f;
			const Rml::Vector2f P12 = (P1 + P2) * 0.5f;
			const Rml::Vector2f P23 = (P2 + P3) * 0.5f;
			const Rml::Vector2f P012 = (P01 + P12) * 0.5f;
			const Rml::Vector2f P123 = (P12 + P23) * 0.5f;
			const Rml::Vector2f Middle = (P012 + P123) * 0.5f;
			Flatten(Out, P0, P01, P012, Middle, Depth + 1);
			Flatten(Out, Middle, P123, P23, P3, Depth + 1);
		}

		/// The parsed path flattened to pixels relative to the content box, kept for the hit test. Empty when nothing
		/// can be placed, such as a view box on a box with no size.
		void FlattenContours()
		{
			Flattened.clear();
			if (Parser.Contours.empty())
			{
				return;
			}

			float Scale = ResolveLength(Rml::NumericValue(1.0f, Rml::Unit::DP));
			Rml::Vector2f Offset(0.0f);
			if (bViewBox)
			{
				const Rml::Vector2f Size = GetBox().GetSize(Rml::BoxArea::Content);
				if (Size.x <= 0.0f || Size.y <= 0.0f)
				{
					return;
				}
				Scale = std::min(Size.x / ViewBox.Width, Size.y / ViewBox.Height);
				Offset = (Size - Rml::Vector2f(ViewBox.Width, ViewBox.Height) * Scale) * 0.5f
					- Rml::Vector2f(ViewBox.X, ViewBox.Y) * Scale;
			}
			const auto Place = [Scale, Offset](Rml::Vector2f Point) { return Point * Scale + Offset; };

			Flattened.resize(Parser.Contours.size());
			for (size_t ContourIndex = 0; ContourIndex < Parser.Contours.size(); ++ContourIndex)
			{
				const Detail::FPathContour& Contour = Parser.Contours[ContourIndex];
				std::vector<Rml::Vector2f>& Points = Flattened[ContourIndex];
				Rml::Vector2f Previous = Place(Contour.Start);
				Points.push_back(Previous);
				for (size_t Index = 0; Index + 2 < Contour.Cubics.size(); Index += 3)
				{
					const Rml::Vector2f A = Place(Contour.Cubics[Index]);
					const Rml::Vector2f B = Place(Contour.Cubics[Index + 1]);
					const Rml::Vector2f End = Place(Contour.Cubics[Index + 2]);
					Flatten(Points, Previous, A, B, End, 0);
					Previous = End;
				}
			}
		}

		/// `path-dash` in pixels: alternating dash and gap lengths, doubled when odd as SVG does. Empty for a solid stroke.
		std::vector<float> DashPattern()
		{
			std::vector<float> Out;
			const Rml::Property* Value = GetProperty(Detail::PathProperties().Dash);
			if (Value == nullptr)
			{
				return Out;
			}
			const Rml::String Text = Value->Get<Rml::String>();
			const char* Cursor = Text.c_str();
			float Total = 0.0f;
			while (true)
			{
				while (*Cursor == ' ' || *Cursor == ',')
				{
					++Cursor;
				}
				char* End = nullptr;
				const float Number = std::strtof(Cursor, &End);
				if (End == Cursor)
				{
					break;
				}
				Cursor = End;
				Rml::Unit Unit = Rml::Unit::DP;
				if (Cursor[0] == 'p' && Cursor[1] == 'x')
				{
					Unit = Rml::Unit::PX;
					Cursor += 2;
				}
				else if (Cursor[0] == 'd' && Cursor[1] == 'p')
				{
					Cursor += 2;
				}
				const float Length = std::max(ResolveLength(Rml::NumericValue(Number, Unit)), 0.0f);
				Out.push_back(Length);
				Total += Length;
			}
			if (Total <= 0.0f)
			{
				Out.clear();
			}
			else if (Out.size() % 2 == 1)
			{
				const size_t Count = Out.size();
				for (size_t Index = 0; Index < Count; ++Index)
				{
					Out.push_back(Out[Index]);
				}
			}
			return Out;
		}

		/// The "on" stretches of a polyline under a dash pattern starting Offset pixels into it. Each stretch is an open
		/// polyline that keeps the corners it passes through.
		static std::vector<std::vector<Rml::Vector2f>> Dash(const std::vector<Rml::Vector2f>& Points, bool bClosed,
			const std::vector<float>& Pattern, float Offset)
		{
			std::vector<std::vector<Rml::Vector2f>> Out;
			std::vector<Rml::Vector2f> Line = Points;
			if (bClosed && Line.size() > 1)
			{
				Line.push_back(Line.front());
			}
			float Period = 0.0f;
			for (float Length : Pattern)
			{
				Period += Length;
			}
			// Where in the pattern the line starts, and how much of that entry is left.
			float Phase = std::fmod(Offset, Period);
			if (Phase < 0.0f)
			{
				Phase += Period;
			}
			size_t Entry = 0;
			// Strictly past an entry, so a pattern that opens with a zero-length dash starts with its dot.
			while (Phase > 0.0f && Phase >= Pattern[Entry])
			{
				Phase -= Pattern[Entry];
				Entry = (Entry + 1) % Pattern.size();
			}
			float Left = Pattern[Entry] - Phase;
			bool bOn = Entry % 2 == 0;
			std::vector<Rml::Vector2f> Current;
			if (bOn && !Line.empty())
			{
				Current.push_back(Line.front());
			}
			for (size_t Index = 0; Index + 1 < Line.size(); ++Index)
			{
				Rml::Vector2f From = Line[Index];
				const Rml::Vector2f To = Line[Index + 1];
				float Segment = (To - From).Magnitude();
				while (Segment > 0.0f)
				{
					if (Left >= Segment)
					{
						Left -= Segment;
						if (bOn)
						{
							Current.push_back(To);
						}
						break;
					}
					const Rml::Vector2f Split = From + (To - From) * (Left / Segment);
					if (bOn)
					{
						Current.push_back(Split);
						Out.push_back(std::move(Current));
						Current.clear();
					}
					else
					{
						Current.push_back(Split);
					}
					Segment -= Left;
					From = Split;
					Entry = (Entry + 1) % Pattern.size();
					Left = Pattern[Entry];
					bOn = !bOn;
				}
			}
			if (bOn && Current.size() > 1)
			{
				Out.push_back(std::move(Current));
			}
			return Out;
		}

		void Build(Rml::Mesh& Mesh)
		{
			FOLIO_TRACE_SCOPE("Folio/Path/Build");

			const Detail::FPathProperties& Ids = Detail::PathProperties();
			FlattenContours();
			HitHalfWidth = 0.0f;
			bHitFill = false;
			if (Flattened.empty())
			{
				return;
			}

			const float Opacity = GetComputedValues().opacity();
			const Rml::Property* FillValue = GetProperty(Ids.Fill);
			const Rml::Colourb FillColour = FillValue != nullptr ? FillValue->Get<Rml::Colourb>() : Rml::Colourb(0, 0, 0, 0);
			if (FillColour.alpha > 0)
			{
				Detail::AddFill(Mesh, Flattened, FillColour.ToPremultiplied(Opacity), bEvenOdd, Feather);
				bHitFill = true;
			}

			const Rml::Property* WidthValue = GetProperty(Ids.Width);
			float Width = WidthValue != nullptr ? ResolveLength(WidthValue->GetNumericValue()) : 1.0f;
			const Rml::Colourb StrokeColour = GetComputedValues().color();
			if (Width <= 0.0f || StrokeColour.alpha == 0)
			{
				return;
			}
			HitHalfWidth = Width * 0.5f;
			float StrokeOpacity = Opacity;
			// Thinner than a pixel draws a pixel at reduced coverage, which reads as the thinner line without breaking up.
			if (Width < 1.0f)
			{
				StrokeOpacity *= Width;
				Width = 1.0f;
			}
			const Rml::Property* JoinValue = GetProperty(Ids.Join);
			const Rml::Property* CapValue = GetProperty(Ids.Cap);
			const FStroke Stroke{ Mesh, Width * 0.5f, StrokeColour.ToPremultiplied(StrokeOpacity),
				JoinValue != nullptr ? JoinValue->Get<int>() : int(JoinMiter),
				CapValue != nullptr ? CapValue->Get<int>() : int(CapButt),
				4.0f };
			const std::vector<float> Pattern = DashPattern();
			const Rml::Property* OffsetValue = GetProperty(Ids.DashOffset);
			const float DashOffset = OffsetValue != nullptr ? ResolveLength(OffsetValue->GetNumericValue()) : 0.0f;
			for (size_t ContourIndex = 0; ContourIndex < Flattened.size(); ++ContourIndex)
			{
				const bool bClosed = Parser.Contours[ContourIndex].bClosed;
				if (Pattern.empty())
				{
					AddContour(Stroke, Flattened[ContourIndex], bClosed);
					continue;
				}
				for (std::vector<Rml::Vector2f>& Piece : Dash(Flattened[ContourIndex], bClosed, Pattern, DashOffset))
				{
					AddContour(Stroke, std::move(Piece), false);
				}
			}
		}

		/// The drawn shape, not the box: inside the fill by its rule, or within the stroke's half width of the line.
		bool IsPointWithinElement(Rml::Vector2f Point) override
		{
			FOLIO_TRACE_SCOPE("Folio/Path/HitTest");

			if (bDirty)
			{
				// Not drawn since it changed, so measure the shape it will have.
				FlattenContours();
			}
			const Rml::Vector2f Local = Point - GetAbsoluteOffset(Rml::BoxArea::Content);
			// A pixel of slack either side, so a hairline can still be pointed at.
			const float Reach = HitHalfWidth > 0.0f ? HitHalfWidth + 1.0f : 0.0f;
			int Winding = 0;
			for (size_t ContourIndex = 0; ContourIndex < Flattened.size(); ++ContourIndex)
			{
				const std::vector<Rml::Vector2f>& Points = Flattened[ContourIndex];
				const size_t Count = Points.size();
				const bool bClosed = ContourIndex < Parser.Contours.size() && Parser.Contours[ContourIndex].bClosed;
				for (size_t Index = 0; Index < Count; ++Index)
				{
					const Rml::Vector2f A = Points[Index];
					const Rml::Vector2f B = Points[(Index + 1) % Count];
					const bool bClosing = Index + 1 == Count;
					if (Reach > 0.0f && (!bClosing || bClosed))
					{
						const Rml::Vector2f Edge = B - A;
						const float LengthSquared = Edge.SquaredMagnitude();
						const float T = LengthSquared > 0.0f ? Rml::Math::Clamp(Dot(Local - A, Edge) / LengthSquared, 0.0f, 1.0f) : 0.0f;
						if ((A + Edge * T - Local).SquaredMagnitude() <= Reach * Reach)
						{
							return true;
						}
					}
					// The fill closes every contour, so the closing edge always counts toward winding.
					if (bHitFill && A.y != B.y && (A.y <= Local.y) != (B.y <= Local.y))
					{
						const float X = A.x + (B.x - A.x) * ((Local.y - A.y) / (B.y - A.y));
						if (X > Local.x)
						{
							Winding += B.y > A.y ? 1 : -1;
						}
					}
				}
			}
			return bHitFill && (bEvenOdd ? (Winding & 1) != 0 : Winding != 0);
		}
	};

	namespace Detail
	{
		/// A point at Angle degrees clockwise from straight up, Radius from Centre.
		inline Rml::Vector2f Polar(float CentreX, float CentreY, float Radius, float Angle)
		{
			const float Radians = Angle * Rml::Math::RMLUI_PI / 180.0f;
			return Rml::Vector2f(CentreX + Radius * std::sin(Radians), CentreY - Radius * std::cos(Radians));
		}

		/// Path data for an arc of Radius from angle From to To, continuing an open path whose pen is already at From.
		/// SVG cannot draw a full turn as one arc, so a sweep near 360 degrees is split in two.
		inline void AppendArc(Rml::String& Out, float CentreX, float CentreY, float Radius, float From, float To)
		{
			const float Sweep = To - From;
			const int Pieces = std::abs(Sweep) > 359.0f ? 2 : 1;
			for (int Piece = 1; Piece <= Pieces; ++Piece)
			{
				const float Angle = From + Sweep * float(Piece) / float(Pieces);
				const Rml::Vector2f End = Polar(CentreX, CentreY, Radius, Angle);
				char Buffer[128];
				std::snprintf(Buffer, sizeof(Buffer), " A %.3f %.3f 0 %d %d %.3f %.3f", double(Radius), double(Radius),
					std::abs(Sweep) / float(Pieces) > 180.0f ? 1 : 0, Sweep >= 0.0f ? 1 : 0, double(End.x), double(End.y));
				Out += Buffer;
			}
		}

		inline Rml::String MoveTo(Rml::Vector2f Point)
		{
			char Buffer[64];
			std::snprintf(Buffer, sizeof(Buffer), "M %.3f %.3f", double(Point.x), double(Point.y));
			return Buffer;
		}

		/// Reads the transform's arguments: the piped value first, then the ones in brackets. False, with a warning, if
		/// there are too few.
		inline bool PathArguments(const char* Name, const Rml::VariantList& Arguments, size_t Needed, float* Out)
		{
			// No arguments at all is RegisterPathTransforms asking whether the transform exists, not a mistake.
			if (Arguments.empty())
			{
				return false;
			}
			if (Arguments.size() < Needed)
			{
				Rml::Log::Message(Rml::Log::LT_WARNING, "'%s' needs %d arguments in brackets and was given %d.", Name,
					int(Needed) - 1, int(Arguments.size()) - 1);
				return false;
			}
			for (size_t Index = 0; Index < Arguments.size() && Index < 8; ++Index)
			{
				Out[Index] = Arguments[Index].Get<float>();
			}
			return true;
		}
	}

	/// Adds `arc` and `wedge` to a data model, so path data that follows a value needs no game code:
	///
	///  - `Fraction | arc(cx, cy, r, start, end)`: an arc of radius r around (cx, cy), from `start` degrees clockwise
	///    from straight up, as far as Fraction of the way to `end`. Fraction is clamped to 0 to 1.
	///  - `Fraction | wedge(cx, cy, r, start, end)`: the same, closed through the centre into a pie slice for a fill.
	///    A sixth argument, an inner radius, makes it a ring segment instead.
	///
	/// Folio calls this for every data model it creates; call it on a model the game creates itself.
	inline void RegisterPathTransforms(Rml::DataModelConstructor& Constructor)
	{
		// Every model in a context shares one register, and registering a name twice is an error.
		Rml::Variant Probe;
		if (Constructor.GetDataTypeRegister() == nullptr
			|| Constructor.GetDataTypeRegister()->GetTransformFuncRegister()->Call("arc", Rml::VariantList(), Probe))
		{
			return;
		}
		Constructor.RegisterTransformFunc("arc", [](const Rml::VariantList& Arguments) -> Rml::Variant
		{
			float A[8] = {};
			if (!Detail::PathArguments("arc", Arguments, 6, A))
			{
				return Rml::Variant(Rml::String());
			}
			const float End = A[4] + (A[5] - A[4]) * Rml::Math::Clamp(A[0], 0.0f, 1.0f);
			Rml::String Out = Detail::MoveTo(Detail::Polar(A[1], A[2], A[3], A[4]));
			Detail::AppendArc(Out, A[1], A[2], A[3], A[4], End);
			return Rml::Variant(Out);
		});
		Constructor.RegisterTransformFunc("wedge", [](const Rml::VariantList& Arguments) -> Rml::Variant
		{
			float A[8] = {};
			if (!Detail::PathArguments("wedge", Arguments, 6, A))
			{
				return Rml::Variant(Rml::String());
			}
			const float End = A[4] + (A[5] - A[4]) * Rml::Math::Clamp(A[0], 0.0f, 1.0f);
			const float Inner = Arguments.size() > 6 ? std::max(A[6], 0.0f) : 0.0f;
			Rml::String Out = Detail::MoveTo(Detail::Polar(A[1], A[2], A[3], A[4]));
			Detail::AppendArc(Out, A[1], A[2], A[3], A[4], End);
			if (Inner > 0.0f)
			{
				const Rml::Vector2f InnerEnd = Detail::Polar(A[1], A[2], Inner, End);
				char Buffer[64];
				std::snprintf(Buffer, sizeof(Buffer), " L %.3f %.3f", double(InnerEnd.x), double(InnerEnd.y));
				Out += Buffer;
				Detail::AppendArc(Out, A[1], A[2], Inner, End, A[4]);
			}
			else
			{
				char Buffer[64];
				std::snprintf(Buffer, sizeof(Buffer), " L %.3f %.3f", double(A[1]), double(A[2]));
				Out += Buffer;
			}
			Out += " Z";
			return Rml::Variant(Out);
		});
	}

	/// Adds `<path>` and its `path-*` properties. Call after Rml::Initialise, as with the decorators.
	inline void RegisterFolioPath()
	{
		Detail::FPathProperties& Ids = Detail::PathProperties();
		if (Ids.bRegistered)
		{
			return;
		}
		Ids.bRegistered = true;

		Ids.Fill = Rml::StyleSheetSpecification::RegisterProperty("path-fill", "transparent", false, false)
			.AddParser("color")
			.GetId();
		Ids.Width = Rml::StyleSheetSpecification::RegisterProperty("path-width", "1dp", false, false)
			.AddParser("length")
			.GetId();
		Ids.Join = Rml::StyleSheetSpecification::RegisterProperty("path-join", "miter", false, false)
			.AddParser("keyword", "miter, round, bevel")
			.GetId();
		Ids.Cap = Rml::StyleSheetSpecification::RegisterProperty("path-cap", "butt", false, false)
			.AddParser("keyword", "butt, round, square")
			.GetId();
		Ids.Dash = Rml::StyleSheetSpecification::RegisterProperty("path-dash", "none", false, false)
			.AddParser("string")
			.GetId();
		Ids.DashOffset = Rml::StyleSheetSpecification::RegisterProperty("path-dash-offset", "0dp", false, false)
			.AddParser("length")
			.GetId();

		auto Instancer = std::make_unique<Rml::ElementInstancerGeneric<FElementPath>>();
		Rml::Factory::RegisterElementInstancer("path", Instancer.get());
		Detail::PathInstancers().push_back(std::move(Instancer));
	}

	/// Drops the instancer. RmlUi holds a raw pointer, so this must happen after shutdown.
	inline void ReleaseFolioPath()
	{
		Detail::PathInstancers().clear();
		Detail::PathProperties().bRegistered = false;
	}
}

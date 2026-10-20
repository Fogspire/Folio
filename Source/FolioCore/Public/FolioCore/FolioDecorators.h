#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/RenderManager.h>

#include <cmath>
#include <memory>
#include <vector>

namespace Folio
{
	namespace Detail
	{
		/// A stroked outline broken into dashes, following the element's own rounded rectangle.
		///
		/// RCSS has no `border-style`, and RmlUi's borders are solid quads with no seam to cut
		/// them at, so this draws its own stroke rather than modifying one. Set `border: none` and
		/// let the decorator be the border; a real border underneath is drawn as well, not instead.
		class FDashedBorder final : public Rml::Decorator
		{
		public:
			Rml::Colourb Colour;
			/// Held unresolved: a length in dp or em means nothing until it meets an element, and
			/// one decorator is shared by every element whose style names it.
			Rml::NumericValue DashLength;
			Rml::NumericValue GapLength;
			Rml::NumericValue Width;
			/// Set for `dotted-border`, where dash and gap both follow the stroke width.
			bool bDotted = false;

			Rml::DecoratorDataHandle GenerateElementData(Rml::Element* Element, Rml::BoxArea) const override
			{
				// Always the border box, whatever paint area the stylesheet asked for. A border
				// that followed the padding box would sit inside the element's own border, which
				// is not a border.
				const Rml::RenderBox Box = Element->GetRenderBox(Rml::BoxArea::Padding);
				const Rml::EdgeSizes Edges = Box.GetBorderWidths();
				const Rml::Vector2f Size = Box.GetFillSize()
					+ Rml::Vector2f(Edges[3] + Edges[1], Edges[0] + Edges[2]);

				const float Stroke = Rml::Math::Max(Element->ResolveLength(Width), 0.0f);
				const float Dash = bDotted ? Stroke : Element->ResolveLength(DashLength);
				const float Gap = bDotted ? Stroke : Element->ResolveLength(GapLength);

				Rml::Mesh Mesh;
				if (Stroke > 0.0f && Dash > 0.0f && Gap > 0.0f && Size.x > 0.0f && Size.y > 0.0f)
				{
					const float Opacity = Element->GetComputedValues().opacity();
					BuildDashes(Mesh, Box.GetBorderOffset(), Size, Box.GetBorderRadius(), Stroke,
						Dash, Gap, Colour.ToPremultiplied(Opacity));
				}

				if (Mesh.indices.empty())
				{
					return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
				}

				auto* Geometry = new Rml::Geometry(Element->GetRenderManager()->MakeGeometry(std::move(Mesh)));
				return reinterpret_cast<Rml::DecoratorDataHandle>(Geometry);
			}

			void ReleaseElementData(Rml::DecoratorDataHandle Handle) const override
			{
				delete reinterpret_cast<Rml::Geometry*>(Handle);
			}

			void RenderElement(Rml::Element* Element, Rml::DecoratorDataHandle Handle) const override
			{
				reinterpret_cast<Rml::Geometry*>(Handle)->Render(Element->GetAbsoluteOffset(Rml::BoxArea::Border));
			}

		private:
			/// Samples along a corner, for turning a distance along it into an angle.
			static constexpr int ArcSamples = 32;

			/// One piece of the outline. A run is a straight edge; an arc is a corner, a quarter
			/// of an ellipse with a horizontal and a vertical radius.
			struct FSegment
			{
				bool bArc = false;
				/// Straight: the two ends. Arc: the centre, and the angle it starts at.
				Rml::Vector2f Start;
				Rml::Vector2f End;
				Rml::Vector2f Centre;
				Rml::Vector2f Radii;
				float StartAngle = 0.0f;
				float Length = 0.0f;
				/// Arc: the distance along it at each of the evenly spaced angles, so a dash keeps
				/// its length where an ellipse is flatter.
				float Distances[ArcSamples + 1] = {};

				Rml::Vector2f PointAt(const float Angle) const
				{
					return Centre + Rml::Vector2f(std::cos(Angle) * Radii.x, std::sin(Angle) * Radii.y);
				}

				/// The outward normal, which on an ellipse is not the direction from the centre.
				Rml::Vector2f NormalAt(const float Angle) const
				{
					return Rml::Vector2f(std::cos(Angle) * Radii.y, std::sin(Angle) * Radii.x).Normalise();
				}

				float AngleAt(const float Distance) const
				{
					constexpr float SampleAngle = Rml::Math::RMLUI_PI * 0.5f / float(ArcSamples);
					int Index = 0;
					while (Index < ArcSamples - 1 && Distances[Index + 1] < Distance)
					{
						++Index;
					}
					const float SampleLength = Distances[Index + 1] - Distances[Index];
					const float Fraction = SampleLength > 0.0f
						? Rml::Math::Clamp((Distance - Distances[Index]) / SampleLength, 0.0f, 1.0f)
						: 0.0f;
					return StartAngle + (float(Index) + Fraction) * SampleAngle;
				}
			};

			static void BuildDashes(Rml::Mesh& Mesh, const Rml::Vector2f Origin, const Rml::Vector2f Size,
				const Rml::CornerSizes2& Radii, const float Stroke, const float Dash, const float Gap,
				const Rml::ColourbPremultiplied Colour)
			{
				const float Half = 0.5f * Stroke;

				// The stroke is centred on a path inset by half its width, so it lands where a
				// solid border of the same width would rather than half outside the element.
				const Rml::Vector2f Inset = Origin + Rml::Vector2f(Half, Half);
				const Rml::Vector2f Span = Size - Rml::Vector2f(Stroke, Stroke);
				if (Span.x <= 0.0f || Span.y <= 0.0f)
				{
					return;
				}

				Rml::Vector2f Corner[4];
				for (int Index = 0; Index < 4; ++Index)
				{
					Corner[Index] = Rml::Math::Max(Radii[Index] - Rml::Vector2f(Half), Rml::Vector2f(0.0f));
					if (Corner[Index].x <= 0.0f || Corner[Index].y <= 0.0f)
					{
						Corner[Index] = Rml::Vector2f(0.0f);
					}
				}

				// Radii are scaled down first, the way a browser does: two corners sharing an edge
				// cannot together be longer than it, or the arcs overlap and the straight run
				// between them has negative length. One factor for all of them, as the background
				// under the stroke uses, so the two curves agree.
				float Scale = 1.0f;
				ShrinkToFit(Scale, Corner[0].x + Corner[1].x, Span.x);
				ShrinkToFit(Scale, Corner[3].x + Corner[2].x, Span.x);
				ShrinkToFit(Scale, Corner[0].y + Corner[3].y, Span.y);
				ShrinkToFit(Scale, Corner[1].y + Corner[2].y, Span.y);
				for (Rml::Vector2f& Radius : Corner)
				{
					Radius *= Scale;
				}

				const float Left = Inset.x;
				const float Top = Inset.y;
				const float Right = Inset.x + Span.x;
				const float Bottom = Inset.y + Span.y;

				// Clockwise from the top-left corner's end, so the path closes on itself.
				std::vector<FSegment> Segments;
				Segments.reserve(8);

				const auto AddRun = [&Segments](Rml::Vector2f From, Rml::Vector2f To)
				{
					const float Length = (To - From).Magnitude();
					if (Length > 0.0f)
					{
						FSegment Run;
						Run.Start = From;
						Run.End = To;
						Run.Length = Length;
						Segments.push_back(Run);
					}
				};

				const auto AddArc = [&Segments](Rml::Vector2f Centre, Rml::Vector2f Radii, float StartAngle)
				{
					if (Radii.x > 0.0f && Radii.y > 0.0f)
					{
						FSegment Arc;
						Arc.bArc = true;
						Arc.Centre = Centre;
						Arc.Radii = Radii;
						Arc.StartAngle = StartAngle;

						// An ellipse has no closed form for its length, so it is measured as a fine
						// polyline. On a circle this is within a thousandth of the exact length.
						constexpr float SampleAngle = Rml::Math::RMLUI_PI * 0.5f / float(ArcSamples);
						Rml::Vector2f Previous = Arc.PointAt(StartAngle);
						for (int Index = 1; Index <= ArcSamples; ++Index)
						{
							const Rml::Vector2f Point = Arc.PointAt(StartAngle + float(Index) * SampleAngle);
							Arc.Distances[Index] = Arc.Distances[Index - 1] + (Point - Previous).Magnitude();
							Previous = Point;
						}
						Arc.Length = Arc.Distances[ArcSamples];
						Segments.push_back(Arc);
					}
				};

				constexpr float HalfPi = Rml::Math::RMLUI_PI * 0.5f;

				AddRun({Left + Corner[0].x, Top}, {Right - Corner[1].x, Top});
				AddArc({Right - Corner[1].x, Top + Corner[1].y}, Corner[1], -HalfPi);
				AddRun({Right, Top + Corner[1].y}, {Right, Bottom - Corner[2].y});
				AddArc({Right - Corner[2].x, Bottom - Corner[2].y}, Corner[2], 0.0f);
				AddRun({Right - Corner[2].x, Bottom}, {Left + Corner[3].x, Bottom});
				AddArc({Left + Corner[3].x, Bottom - Corner[3].y}, Corner[3], HalfPi);
				AddRun({Left, Bottom - Corner[3].y}, {Left, Top + Corner[0].y});
				AddArc({Left + Corner[0].x, Top + Corner[0].y}, Corner[0], Rml::Math::RMLUI_PI);

				float Perimeter = 0.0f;
				for (const FSegment& Segment : Segments)
				{
					Perimeter += Segment.Length;
				}
				if (Perimeter <= 0.0f)
				{
					return;
				}

				// The period is stretched so a whole number of them fits exactly. Without this the
				// last dash before the path closes is cut at an arbitrary length and meets the
				// first one at a visible seam, which no amount of tuning the dash length hides.
				const float Wanted = Dash + Gap;
				const float Count = Rml::Math::Max(std::round(Perimeter / Wanted), 1.0f);
				const float Period = Perimeter / Count;
				const float DashSpan = Period * (Dash / Wanted);

				for (float Start = 0.0f; Start < Perimeter - 0.001f; Start += Period)
				{
					EmitStroke(Mesh, Segments, Start, Rml::Math::Min(Start + DashSpan, Perimeter), Half, Colour);
				}
			}

			/// Lowers the scale until two radii totalling `Total` fit along an edge of `Extent`.
			static void ShrinkToFit(float& Scale, const float Total, const float Extent)
			{
				if (Total > Extent && Total > 0.0f)
				{
					Scale = Rml::Math::Min(Scale, Extent / Total);
				}
			}

			/// Draws the piece of the outline between two distances along it.
			static void EmitStroke(Rml::Mesh& Mesh, const std::vector<FSegment>& Segments, const float From,
				const float To, const float Half, const Rml::ColourbPremultiplied Colour)
			{
				float Travelled = 0.0f;
				for (const FSegment& Segment : Segments)
				{
					const float SegmentFrom = Rml::Math::Max(From - Travelled, 0.0f);
					const float SegmentTo = Rml::Math::Min(To - Travelled, Segment.Length);
					Travelled += Segment.Length;

					if (SegmentTo <= SegmentFrom)
					{
						continue;
					}

					if (!Segment.bArc)
					{
						const Rml::Vector2f Direction = (Segment.End - Segment.Start) / Segment.Length;
						const Rml::Vector2f Normal(-Direction.y, Direction.x);
						AddQuad(Mesh, Segment.Start + Direction * SegmentFrom,
							Segment.Start + Direction * SegmentTo, Normal * Half, Colour);
						continue;
					}

					// Enough steps that the outer edge never bows in by more than about a fifth of
					// a pixel, which is below what a distance of one pixel could show.
					const float Angle0 = Segment.AngleAt(SegmentFrom);
					const float SweptAngle = Segment.AngleAt(SegmentTo) - Angle0;
					const float Radius = Rml::Math::Max(Segment.Radii.x, Segment.Radii.y);
					const int Steps = Rml::Math::Clamp(
						int(std::ceil(SweptAngle / 0.2f * Rml::Math::Max(Radius / 8.0f, 1.0f))), 1, 64);

					float Angle = Angle0;
					const float Step = SweptAngle / float(Steps);

					for (int Index = 0; Index < Steps; ++Index)
					{
						const float Next = Angle + Step;
						AddArcQuad(Mesh, Segment, Half, Angle, Next, Colour);
						Angle = Next;
					}
				}
			}

			static void AddQuad(Rml::Mesh& Mesh, const Rml::Vector2f From, const Rml::Vector2f To,
				const Rml::Vector2f Offset, const Rml::ColourbPremultiplied Colour)
			{
				AddQuad(Mesh, From - Offset, From + Offset, To + Offset, To - Offset, Colour);
			}

			static void AddArcQuad(Rml::Mesh& Mesh, const FSegment& Arc, const float Half, const float From,
				const float To, const Rml::ColourbPremultiplied Colour)
			{
				const Rml::Vector2f A = Arc.PointAt(From);
				const Rml::Vector2f B = Arc.PointAt(To);
				const Rml::Vector2f NormalA = Arc.NormalAt(From) * Half;
				const Rml::Vector2f NormalB = Arc.NormalAt(To) * Half;

				AddQuad(Mesh, A - NormalA, A + NormalA, B + NormalB, B - NormalB, Colour);
			}

			static void AddQuad(Rml::Mesh& Mesh, const Rml::Vector2f A, const Rml::Vector2f B,
				const Rml::Vector2f C, const Rml::Vector2f D, const Rml::ColourbPremultiplied Colour)
			{
				const int Base = int(Mesh.vertices.size());

				for (const Rml::Vector2f Position : {A, B, C, D})
				{
					Rml::Vertex& Vertex = Mesh.vertices.emplace_back();
					Vertex.position = Position;
					Vertex.colour = Colour;
					Vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
				}

				for (const int Offset : {0, 1, 2, 0, 2, 3})
				{
					Mesh.indices.push_back(Base + Offset);
				}
			}
		};

		class FDashedBorderInstancer final : public Rml::DecoratorInstancer
		{
		public:
			FDashedBorderInstancer()
			{
				Ids.Colour = RegisterProperty("color", "#ffffff").AddParser("color").GetId();
				Ids.Width = RegisterProperty("width", "1dp").AddParser("length").GetId();
				Ids.Dash = RegisterProperty("dash-length", "6dp").AddParser("length").GetId();
				Ids.Gap = RegisterProperty("gap-length", "4dp").AddParser("length").GetId();

				RegisterShorthand("decorator", "color, width, dash-length, gap-length",
					Rml::ShorthandType::FallThrough);
			}

			Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String& Name,
				const Rml::PropertyDictionary& Properties, const Rml::DecoratorInstancerInterface&) override
			{
				auto Decorator = Rml::MakeShared<FDashedBorder>();
				Decorator->bDotted = Name == "dotted-border";
				Decorator->Colour = Properties.GetProperty(Ids.Colour)->Get<Rml::Colourb>();
				Decorator->Width = Properties.GetProperty(Ids.Width)->GetNumericValue();
				Decorator->DashLength = Properties.GetProperty(Ids.Dash)->GetNumericValue();
				Decorator->GapLength = Properties.GetProperty(Ids.Gap)->GetNumericValue();

				return Decorator;
			}

		private:
			struct FIds
			{
				Rml::PropertyId Colour{}, Width{}, Dash{}, Gap{};
			};

			FIds Ids;
		};

		inline std::vector<std::unique_ptr<Rml::DecoratorInstancer>>& OwnedDecorators()
		{
			static std::vector<std::unique_ptr<Rml::DecoratorInstancer>> Owned;
			return Owned;
		}
	}

	/// Adds `dashed-border` and `dotted-border`, which RCSS has no `border-style` for.
	///
	/// Call from the module that called Rml::Initialise, after it. Inline deliberately: RmlUi is a
	/// static library, so the factory these register into is the calling module's own.
	inline void RegisterFolioDecorators()
	{
		auto& Owned = Detail::OwnedDecorators();
		if (!Owned.empty())
		{
			return;
		}

		for (const char* Name : { "dashed-border", "dotted-border" })
		{
			auto Instancer = std::make_unique<Detail::FDashedBorderInstancer>();
			Rml::Factory::RegisterDecoratorInstancer(Name, Instancer.get());
			Owned.push_back(std::move(Instancer));
		}
	}

	/// Drops the instancers. RmlUi holds raw pointers, so this must happen after shutdown.
	inline void ReleaseFolioDecorators()
	{
		Detail::OwnedDecorators().clear();
	}
}

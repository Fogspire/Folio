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
#include <cmath>
#include <cstdlib>
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

	/// `<line-graph>`: a line through a series of values, coloured per point, with a dot on the newest and an optional
	/// average line.
	///
	/// Attributes, all bindable with `data-attr-*`:
	///  - `values`: heights from 0 (bottom of the content box) to 1 (top), oldest first, spread evenly across the width.
	///  - `tones`: one per value, from -1 to 1. -1 draws in `graph-low-color`, 0 in `graph-mid-color` and 1 in
	///    `graph-high-color`, blending smoothly between them. Missing tones draw at 0.
	///  - `average`: a height from 0 to 1 for a horizontal line in `graph-average-color`. Leave it out for none.
	///  - `length`: how many values fill the width. Fewer sit at the right edge, so a history that is still filling
	///    grows leftwards instead of stretching. Leave it out to spread whatever there is across the width.
	///
	/// Properties: `graph-low-color`, `graph-mid-color`, `graph-high-color`, `graph-average-color`, `graph-line-width`
	/// and `graph-dot-size` (the dot's diameter, 0 for none).
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
			const Rml::Colourb Low = GetColour(Ids.LowColour, Rml::Colourb(229, 83, 75));
			const Rml::Colourb Mid = GetColour(Ids.MidColour, Rml::Colourb(78, 161, 243));
			const Rml::Colourb High = GetColour(Ids.HighColour, Rml::Colourb(87, 211, 140));

			// Inset by the dot, so neither it nor the stroke is cut off at the edges.
			const float Inset = std::max(Radius, HalfWidth) + Feather;
			const auto Place = [&Size, Inset](float Fraction, float Height)
			{
				return Rml::Vector2f(Inset + Fraction * (Size.x - 2.0f * Inset),
					Inset + (1.0f - Rml::Math::Clamp(Height, 0.0f, 1.0f)) * (Size.y - 2.0f * Inset));
			};

			if (bAverage)
			{
				const Rml::Colourb Colour = GetColour(Ids.AverageColour, Rml::Colourb(255, 255, 255, 90));
				const std::vector<Rml::Vector2f> Line = { Place(0.0f, Average), Place(1.0f, Average) };
				const std::vector<Rml::ColourbPremultiplied> Colours(2, Colour.ToPremultiplied(Opacity));
				AddStrip(Mesh, Line, Colours, 0.5f);
			}

			const int Count = int(Values.size());
			if (Count == 0)
			{
				return;
			}

			const int Slots = std::max(Length, Count);
			std::vector<Rml::Vector2f> Points(Count);
			std::vector<Rml::ColourbPremultiplied> Colours(Count);
			for (int Index = 0; Index < Count; ++Index)
			{
				const int Slot = Slots - Count + Index;
				Points[Index] = Place(Slots > 1 ? float(Slot) / float(Slots - 1) : 1.0f, Values[Index]);
				const float Tone = Rml::Math::Clamp(Index < int(Tones.size()) ? Tones[Index] : 0.0f, -1.0f, 1.0f);
				const Rml::Colourb Colour = Tone < 0.0f ? Lerp(Mid, Low, -Tone) : Lerp(Mid, High, Tone);
				Colours[Index] = Colour.ToPremultiplied(Opacity);
			}

			if (Count > 1)
			{
				AddStrip(Mesh, Points, Colours, HalfWidth);
			}
			if (Radius > 0.0f)
			{
				AddDot(Mesh, Points.back(), Radius, Colours.back());
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

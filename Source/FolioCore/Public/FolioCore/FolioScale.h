#pragma once

#include "FolioCore/FolioCoreAPI.h"
#include "FolioCore/FolioTrace.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/XMLParser.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace Folio
{
	namespace Detail
	{
		inline std::vector<std::unique_ptr<Rml::ElementInstancer>>& ScaleInstancers()
		{
			static std::vector<std::unique_ptr<Rml::ElementInstancer>> Instancers;
			return Instancers;
		}

		/// Numbers separated by spaces or commas. Anything unreadable ends the list.
		inline std::vector<float> ParseScaleNumbers(const Rml::String& Text)
		{
			std::vector<float> Out;
			const char* Cursor = Text.c_str();
			while (true)
			{
				while (*Cursor == ' ' || *Cursor == ',' || *Cursor == '\t' || *Cursor == '\n')
				{
					++Cursor;
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
			return Out;
		}
	}

	/// `<scale>`: the ticks and numbers of a gauge, laid along a line or an arc. It writes them as children, so they are
	/// styled like any other element: `<path class="minor">` holds every minor tick, `<path class="major">` every major
	/// one, and each number is a `<span class="label">`.
	///
	/// Attributes, all bindable with `data-attr-*`; positions and lengths are in dp from the scale's own corner:
	///  - `from`, `to`: the values at either end.
	///  - `step`, `major`: the spacing of minor and major ticks, counted from `from`. A minor tick is not drawn where a
	///    major one is.
	///  - `minor-values`, `major-values`: explicit lists instead, for a scale whose marks are not evenly spaced.
	///  - `kind`: `linear` (the default) or `arc`.
	///  - Linear: `start` and `end`, the points `from` and `to` sit at, as `x y`.
	///  - Arc: `centre` as `x y`, `radius`, and `start-angle` and `end-angle` in degrees, clockwise from straight up.
	///  - `minor-length`, `major-length`: how long the ticks are.
	///  - `side`: which way ticks grow from the line. `right` (the default) or `left` of the direction from `from` to
	///    `to` on a linear scale; `in` (the default) or `out` on an arc; `centre` straddles the line on either.
	///  - `labels`: `major` (the default) puts a number on every major tick, `none` on none, or a list of values.
	///  - `label-offset`: how far the numbers sit from the line, measured the way the ticks grow.
	///  - `label-anchor`: `centre` (the default), `start` or `end`: which part of the number sits on its point, along
	///    the line's normal for a linear scale and horizontally for an arc.
	///  - `label-decimals`: decimal places. Left out, a number shows as many as it needs.
	///  - `label-class`: classes added to every label, for the game's own text styles.
	class FElementScale : public Rml::Element
	{
	public:
		explicit FElementScale(const Rml::String& Tag)
			: Rml::Element(Tag)
		{
		}

	protected:
		void OnAttributeChange(const Rml::ElementAttributes& Changed) override
		{
			Rml::Element::OnAttributeChange(Changed);
			bDirty = true;
		}

		void OnUpdate() override
		{
			Rml::Element::OnUpdate();
			if (bDirty)
			{
				bDirty = false;
				Generate();
			}
		}

	private:
		bool bDirty = true;
		std::vector<Rml::Element*> Generated;

		Rml::String Text(const char* Name, const char* Fallback)
		{
			return GetAttribute<Rml::String>(Name, Fallback);
		}

		float Number(const char* Name, float Fallback)
		{
			const Rml::String Value = GetAttribute<Rml::String>(Name, "");
			return Value.empty() ? Fallback : std::strtof(Value.c_str(), nullptr);
		}

		Rml::Vector2f Point(const char* Name)
		{
			const std::vector<float> Numbers = Detail::ParseScaleNumbers(GetAttribute<Rml::String>(Name, ""));
			return Numbers.size() >= 2 ? Rml::Vector2f(Numbers[0], Numbers[1]) : Rml::Vector2f(0.0f);
		}

		static std::vector<float> Steps(float From, float To, float Step)
		{
			std::vector<float> Out;
			if (!(Step > 0.0f) || From == To)
			{
				return Out;
			}
			const float Direction = To > From ? 1.0f : -1.0f;
			const float Span = std::abs(To - From);
			const int Count = int(std::floor(Span / Step + 1e-4f));
			if (Count > 10000)
			{
				return Out;
			}
			for (int Index = 0; Index <= Count; ++Index)
			{
				Out.push_back(From + Direction * Step * float(Index));
			}
			return Out;
		}

		static bool Contains(const std::vector<float>& Values, float Value, float Tolerance)
		{
			for (float Candidate : Values)
			{
				if (std::abs(Candidate - Value) <= Tolerance)
				{
					return true;
				}
			}
			return false;
		}

		static Rml::String Format(float Value, int Decimals)
		{
			char Buffer[64];
			if (Decimals >= 0)
			{
				std::snprintf(Buffer, sizeof(Buffer), "%.*f", Decimals, double(Value));
			}
			else
			{
				std::snprintf(Buffer, sizeof(Buffer), "%g", double(std::round(Value * 1000.0f) / 1000.0f));
			}
			// A rounded negative zero reads as "-0".
			Rml::String Out(Buffer);
			if (Out.size() > 1 && Out[0] == '-' && Out.find_first_not_of("-0.") == Rml::String::npos)
			{
				Out.erase(0, 1);
			}
			return Out;
		}

		static void Append(Rml::String& Path, Rml::Vector2f A, Rml::Vector2f B)
		{
			char Buffer[96];
			std::snprintf(Buffer, sizeof(Buffer), "M %.2f %.2f L %.2f %.2f ", double(A.x), double(A.y), double(B.x), double(B.y));
			Path += Buffer;
		}

		void Clear()
		{
			for (Rml::Element* Child : Generated)
			{
				RemoveChild(Child);
			}
			Generated.clear();
		}

		Rml::Element* Add(const char* Tag, const Rml::String& Class)
		{
			Rml::ElementPtr Child = Rml::Factory::InstanceElement(this, Tag, Tag, Rml::XMLAttributes());
			if (!Child)
			{
				return nullptr;
			}
			Child->SetClassNames(Class);
			Rml::Element* Added = AppendChild(std::move(Child));
			Generated.push_back(Added);
			return Added;
		}

		void Generate()
		{
			FOLIO_TRACE_SCOPE("Folio/Scale/Generate");

			Clear();
			const float From = Number("from", 0.0f);
			const float To = Number("to", 1.0f);
			if (From == To)
			{
				return;
			}
			const float Tolerance = std::abs(To - From) * 1e-5f;

			const Rml::String MajorList = Text("major-values", "");
			const Rml::String MinorList = Text("minor-values", "");
			const std::vector<float> Majors = !MajorList.empty() ? Detail::ParseScaleNumbers(MajorList) : Steps(From, To, Number("major", 0.0f));
			std::vector<float> Minors;
			for (float Value : !MinorList.empty() ? Detail::ParseScaleNumbers(MinorList) : Steps(From, To, Number("step", 0.0f)))
			{
				if (!Contains(Majors, Value, Tolerance))
				{
					Minors.push_back(Value);
				}
			}

			const bool bArc = Text("kind", "linear") == "arc";
			const Rml::String Side = Text("side", bArc ? "in" : "right");
			const Rml::Vector2f Start = Point("start");
			const Rml::Vector2f End = Point("end");
			const Rml::Vector2f Centre = Point("centre");
			const float Radius = Number("radius", 0.0f);
			const float StartAngle = Number("start-angle", 0.0f);
			const float EndAngle = Number("end-angle", 360.0f);
			const Rml::Vector2f Along = (End - Start).Normalise();
			// Right of travel, with y down.
			const Rml::Vector2f Right(-Along.y, Along.x);

			// Where a value sits on the line, and the unit vector ticks grow along from it.
			const auto Place = [&](float Value, Rml::Vector2f& Position, Rml::Vector2f& Growth)
			{
				const float T = (Value - From) / (To - From);
				if (bArc)
				{
					const float Angle = (StartAngle + (EndAngle - StartAngle) * T) * Rml::Math::RMLUI_PI / 180.0f;
					const Rml::Vector2f Outward(std::sin(Angle), -std::cos(Angle));
					Position = Centre + Outward * Radius;
					Growth = Side == "out" ? Outward : -Outward;
				}
				else
				{
					Position = Start + (End - Start) * T;
					Growth = Side == "left" ? -Right : Right;
				}
			};
			const bool bCentred = Side == "centre" || Side == "center";
			const auto TickPath = [&](const std::vector<float>& Values, float Length)
			{
				Rml::String Path;
				for (float Value : Values)
				{
					Rml::Vector2f Position;
					Rml::Vector2f Growth;
					Place(Value, Position, Growth);
					if (bCentred)
					{
						Append(Path, Position - Growth * (Length * 0.5f), Position + Growth * (Length * 0.5f));
					}
					else
					{
						Append(Path, Position, Position + Growth * Length);
					}
				}
				return Path;
			};

			const float MinorLength = Number("minor-length", 6.0f);
			const float MajorLength = Number("major-length", 10.0f);
			const auto AddTicks = [&](const std::vector<float>& Values, float Length, const char* Class)
			{
				if (Values.empty() || Length <= 0.0f)
				{
					return;
				}
				if (Rml::Element* Path = Add("path", Class))
				{
					Path->SetAttribute("style", "position: absolute; left: 0dp; top: 0dp;");
					Path->SetAttribute("d", TickPath(Values, Length));
				}
			};
			AddTicks(Minors, MinorLength, "minor");
			AddTicks(Majors, MajorLength, "major");

			const Rml::String Labels = Text("labels", "major");
			if (Labels == "none")
			{
				return;
			}
			const std::vector<float> LabelValues = Labels == "major" ? Majors : Detail::ParseScaleNumbers(Labels);
			const float Offset = Number("label-offset", MajorLength + 8.0f);
			const int Decimals = GetAttribute<Rml::String>("label-decimals", "").empty() ? -1 : GetAttribute<int>("label-decimals", 0);
			const Rml::String Anchor = Text("label-anchor", "centre");
			const Rml::String Class = Rml::String("label ") + Text("label-class", "");
			for (float Value : LabelValues)
			{
				Rml::Vector2f Position;
				Rml::Vector2f Growth;
				Place(Value, Position, Growth);
				const Rml::Vector2f At = Position + Growth * Offset;
				// The scale has no size, so a label could not shrink to fit its text. It gets a box wide enough for any number
				// instead, aligned so the anchored part of the text lands on the point.
				const char* Box = "width: 1000dp; margin-left: -500dp; text-align: center;";
				if (Anchor == "start" || Anchor == "end")
				{
					// Along the normal on a line, so a vertical tape's numbers line up at their left or right edge.
					const float Sign = bArc ? 1.0f : (Growth.x >= 0.0f ? 1.0f : -1.0f);
					const bool bLeftEdge = (Anchor == "start") == (Sign > 0.0f);
					Box = bLeftEdge ? "width: 1000dp; margin-left: 0dp; text-align: left;" : "width: 1000dp; margin-left: -1000dp; text-align: right;";
				}
				Rml::Element* Label = Add("span", Class);
				if (Label == nullptr)
				{
					continue;
				}
				char Style[256];
				std::snprintf(Style, sizeof(Style),
					"position: absolute; left: %.2fdp; top: %.2fdp; %s transform: translateY(-50%%); pointer-events: none;",
					double(At.x), double(At.y), Box);
				Label->SetAttribute("style", Rml::String(Style));
				Label->SetAttribute("data-noloc", "");
				Label->SetInnerRML(Format(Value, Decimals));
			}
		}
	};

	/// Adds `<scale>`. It registers no properties: its ticks are `<path>` elements, so `RegisterFolioPath` must run too.
	inline void RegisterFolioScale()
	{
		if (!Detail::ScaleInstancers().empty())
		{
			return;
		}
		auto Instancer = std::make_unique<Rml::ElementInstancerGeneric<FElementScale>>();
		Rml::Factory::RegisterElementInstancer("scale", Instancer.get());
		Detail::ScaleInstancers().push_back(std::move(Instancer));
	}

	/// Drops the instancer. RmlUi holds a raw pointer, so this must happen after shutdown.
	inline void ReleaseFolioScale()
	{
		Detail::ScaleInstancers().clear();
	}
}

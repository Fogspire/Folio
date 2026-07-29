#include "MetricsReport.h"

#include "App.h"
#include "Diagnostics.h"
#include "ElementPath.h"
#include "Options.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

using ordered_json = nlohmann::ordered_json;

namespace uipreview {

namespace {

// Two decimals. Layout is computed in floats and the last bits are noise, so a raw dump would
// diff dirty on runs that laid out identically.
double Round2(float value)
{
	return std::round((double)value * 100.0) / 100.0;
}

const char* StyleName(Rml::Style::FontStyle style)
{
	return style == Rml::Style::FontStyle::Italic ? "italic" : "normal";
}

ordered_json DescribeFont(Rml::Element* element)
{
	const auto& computed = element->GetComputedValues();
	return ordered_json{
		{"family", computed.font_family()},
		{"size", Round2(computed.font_size())},
		{"weight", (int)computed.font_weight()},
		{"style", StyleName(computed.font_style())},
		{"line_height", Round2(computed.line_height().value)},
		{"letter_spacing", Round2(computed.letter_spacing())},
	};
}

ordered_json DescribeRect(Rml::Element* element)
{
	const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
	return ordered_json::array({Round2(offset.x), Round2(offset.y), Round2(size.x), Round2(size.y)});
}

void Walk(Rml::Element* element, ordered_json& out_entries, int& out_lines)
{
	if (!element)
		return;

	if (Rml::ElementText* text = rmlui_dynamic_cast<Rml::ElementText*>(element))
	{
		// The parent is what carries the id, the classes and the styling. A text node has none of
		// them, so naming it by its own path would produce something no one could act on.
		Rml::Element* owner = text->GetParentNode() ? text->GetParentNode() : text;

		ordered_json lines = ordered_json::array();
		const Rml::Vector2f origin = text->GetAbsoluteOffset(Rml::BoxArea::Border);
		for (const Rml::ElementText::Line& line : text->GetLines())
		{
			lines.push_back({
				{"text", line.text},
				{"x", Round2(origin.x + line.position.x)},
				{"y", Round2(origin.y + line.position.y)},
				{"width", line.width},
			});
			++out_lines;
		}

		out_entries.push_back({
			{"path", ComputeElementPath(owner)},
			{"tag", owner->GetTagName()},
			{"classes", owner->GetClassNames()},
			{"rect", DescribeRect(owner)},
			{"font", DescribeFont(text)},
			{"text", text->GetText()},
			{"lines", std::move(lines)},
		});
	}

	for (int i = 0; i < element->GetNumChildren(); ++i)
		Walk(element->GetChild(i), out_entries, out_lines);
}

} // namespace

bool WriteMetricsReport(const std::string& path, const Options& options, const App& app,
	const Diagnostics& diagnostics, std::string& out_error)
{
	Rml::ElementDocument* document = app.GetDocument();
	if (!document)
	{
		out_error = "no document was loaded";
		return false;
	}

	ordered_json report;
	report["schema"] = 1;
	report["tool"] = {{"name", "uipreview"}, {"rmlui", Rml::GetVersion()}};

	// Everything that moves text, so a diff between two reports cannot be blamed on a setting
	// that quietly differed between the runs.
	//
	// The size and ratio are what the context ended up with, not what was asked for. A window
	// larger than the display is silently clamped, and without this the report would claim a
	// resolution the text never laid out at.
	Rml::Context* context = app.GetContext();
	const Rml::Vector2i dimensions = context ? context->GetDimensions() : Rml::Vector2i(0, 0);
	report["command"] = {
		{"document", diagnostics.Relative(options.document)},
		{"requested_size", ordered_json::array({options.width, options.height})},
		{"size", ordered_json::array({dimensions.x, dimensions.y})},
		{"dp_ratio", context ? Round2(context->GetDensityIndependentPixelRatio()) : 0.0},
		{"culture", options.culture},
		{"msdf_px_em", options.msdf_px_em},
		{"msdf_px_range", options.msdf_px_range},
	};

	ordered_json entries = ordered_json::array();
	int line_count = 0;
	Walk(document, entries, line_count);

	report["totals"] = {{"text_elements", (int)entries.size()}, {"lines", line_count}};
	report["text"] = std::move(entries);

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		out_error = "could not open for writing";
		return false;
	}
	file << report.dump(2) << "\n";
	return true;
}

} // namespace uipreview

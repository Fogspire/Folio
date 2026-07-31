#include "ProvenanceReport.h"

#include "App.h"
#include "ElementPath.h"
#include "Options.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/PropertyIdSet.h>
#include <RmlUi/Core/StyleSheetSpecification.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

using ordered_json = nlohmann::ordered_json;

namespace uipreview {

namespace {

double Round2(float value)
{
	return std::round((double)value * 100.0) / 100.0;
}

ordered_json DescribeRect(Rml::Element* element)
{
	const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
	return ordered_json::array({Round2(offset.x), Round2(offset.y), Round2(size.x), Round2(size.y)});
}

void Walk(Rml::Element* element, ordered_json& out_entries)
{
	if (!element)
		return;

	ordered_json properties = ordered_json::array();

	// Every registered property, filtered to those a rule actually set. Asking for all of them and
	// discarding the rest is the only way round: RmlUi resolves a property through the element's
	// definition, and there is no "what did this element's rules provide" that includes what it
	// inherited from a rule matching an ancestor.
	for (const Rml::PropertyId id : Rml::StyleSheetSpecification::GetRegisteredProperties())
	{
		const Rml::Property* property = element->GetProperty(id);
		if (!property || !property->source)
			continue;

		properties.push_back({
			{"name", Rml::StyleSheetSpecification::GetPropertyName(id)},
			{"value", property->ToString()},
			{"rule", property->source->rule_name},
			{"file", property->source->path},
			{"line", property->source->line_number},
		});
	}

	// Every element, not only the styled ones. A tool selecting by clicking needs a box for anything
	// clickable, and an element with no rule of its own is still something an author points at - it is
	// exactly the case where the answer is "the rule you want belongs to an ancestor".
	const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
	if (size.x > 0.0f && size.y > 0.0f)
	{
		out_entries.push_back({
			{"path", ComputeElementPath(element)},
			{"tag", element->GetTagName()},
			{"id", element->GetId()},
			{"classes", element->GetClassNames()},
			{"rect", DescribeRect(element)},
			{"properties", std::move(properties)},
		});
	}

	for (int i = 0; i < element->GetNumChildren(); ++i)
		Walk(element->GetChild(i), out_entries);
}

} // namespace

bool WriteProvenanceReport(const std::string& path, const Options& options, const App& app,
	std::string& out_error)
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
	report["command"] = {{"document", options.document}};

	ordered_json entries = ordered_json::array();
	Walk(document, entries);

	report["totals"] = {{"elements", (int)entries.size()}};
	report["elements"] = std::move(entries);

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

#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace Rml {
class Element;
} // namespace Rml

namespace uipreview {

class App;
struct Options;

/**
 * Writes which rule set every property that a stylesheet actually set, and where that rule lives.
 *
 * The seam an editing tool needs and cannot be given later. Selecting an element in a preview and
 * dragging it is only useful if the edit lands in the source, and landing it means knowing which rule
 * won - not merely what the computed value is. `left: 40%` on `#win` and `left: 40%` inherited from
 * `.window` look identical in a render and are entirely different edits.
 *
 * It also answers the question a stylesheet raises hourly on its own: why is this element like that.
 *
 * Only properties with a source are reported. Every element has a computed value for every registered
 * property, and a dump of those is noise: the ones that came from a rule are the ones anyone can act
 * on, and the count drops from hundreds per element to a handful.
 */
/** Every property a rule set on this one element, each with its value, rule, file and line. */
nlohmann::ordered_json DescribeRuleProperties(Rml::Element* element);

bool WriteProvenanceReport(const std::string& path, const Options& options, const App& app,
	std::string& out_error);

} // namespace uipreview

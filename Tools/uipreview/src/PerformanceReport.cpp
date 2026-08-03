#include "PerformanceReport.h"

#include "App.h"
#include "Diagnostics.h"
#include "ElementPath.h"
#include "Options.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cmath>
#include <string>
#include <vector>

namespace uipreview {
namespace {

/**
 * How much of the viewport an element has to cover before its layer effect is called full-screen.
 *
 * Well above a large dialog and well below a wash, so a panel with a drop shadow is not swept up in
 * this. The case it exists for covers the whole viewport, and the ones close behind it - a letterbox
 * band, a rounded vignette inset a little - still pay almost the whole cost.
 */
constexpr float FullScreenArea = 0.75f;

struct Finding {
	std::string path;
	std::string properties;
	float coverage = 0.0f;
	bool animated = false;
};

/** The layer-forcing properties this element has, in the order they are worth naming. */
std::string LayerProperties(const Rml::ComputedValues& computed)
{
	std::string out;
	const auto add = [&out](const char* name) {
		if (!out.empty())
			out += ", ";
		out += name;
	};

	if (computed.has_box_shadow())
		add("box-shadow");
	if (computed.has_filter())
		add("filter");
	if (computed.has_backdrop_filter())
		add("backdrop-filter");
	if (computed.has_mask_image())
		add("mask-image");

	return out;
}

void Walk(Rml::Element* element, const Rml::Vector2f viewport, std::vector<Finding>& out)
{
	if (!element)
		return;

	if (element->IsVisible())
	{
		const std::string properties = LayerProperties(element->GetComputedValues());
		if (!properties.empty() && viewport.x > 0.0f && viewport.y > 0.0f)
		{
			const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
			const float coverage = (size.x * size.y) / (viewport.x * viewport.y);

			if (coverage >= FullScreenArea)
			{
				// A running animation is what turns one expensive frame into every frame, because a
				// document with one never idles and the target is resolved again each time.
				const Rml::AnimationList* animation = element->GetComputedValues().animation();
				out.push_back({ComputeElementPath(element), properties, coverage,
					animation && !animation->empty()});
			}
		}
	}

	for (int i = 0; i < element->GetNumChildren(); ++i)
		Walk(element->GetChild(i), viewport, out);
}

} // namespace

void CheckPerformance(const Options& options, App& app, Diagnostics& diagnostics)
{
	Rml::ElementDocument* document = app.GetDocument();
	Rml::Context* context = app.GetContext();
	if (!document || !context)
		return;

	// A document whose job is to be baked is expected to be expensive: it is rendered once, by a
	// tool, to produce the texture that replaces it. Saying so in the document is the only place the
	// distinction can live, since nothing about the markup itself differs. On the body rather than
	// in a meta, because RmlUi keeps the body in the DOM and does not keep the head.
	if (document->HasAttribute("folio-bake-source"))
		return;

	const Rml::Vector2i dimensions = context->GetDimensions();
	std::vector<Finding> findings;
	Walk(document, Rml::Vector2f(float(dimensions.x), float(dimensions.y)), findings);

	for (const Finding& finding : findings)
	{
		const int percent = int(std::lround(finding.coverage * 100.0f));

		std::string message = finding.path + ": " + finding.properties + " on an element covering "
			+ std::to_string(percent) + "% of the viewport costs a full-screen render target";
		message += finding.animated
			? ", and it is animating, so that is one every frame"
			: ", every frame the document draws";
		message += ". Bake it to a PNG and draw that with an image decorator: one quad, no target, and"
			" the same picture. A gradient decorator is cheaper still where the shape suits one - it is"
			" elliptical, so it does not stand in for a shadow following a rounded rectangle.";

		diagnostics.Report(Severity::Warning, "W-PERF-FULLSCREENLAYER", std::move(message),
			options.document, 0,
			{{"path", finding.path}, {"properties", finding.properties}, {"coverage", percent},
				{"animated", finding.animated}});
	}
}

} // namespace uipreview

#include "PerformanceReport.h"

#include "App.h"
#include "Diagnostics.h"
#include "ElementPath.h"
#include "Options.h"

#include <RmlUi/Core/Animation.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/PropertyIdSet.h>

#include <cmath>
#include <string>
#include <vector>

namespace uipreview {
namespace {

/**
 * How much of the viewport makes a layer effect a full-screen one.
 *
 * Well above a large dialog and well below a wash. Size is not what makes a layer expensive - the
 * pass costs about the same at any size - but a full-screen one also costs the memory, and it is
 * the one shape that is never worth keeping.
 */
constexpr float FullScreenArea = 0.75f;

/** Above this many layer effects in one document, the fixed per-pass cost is the whole budget. */
constexpr int CrowdedLayerCount = 4;

struct Finding {
	std::string path;
	std::string properties;
	float coverage = 0.0f;
	bool animated = false;
	bool resizes = false;
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

/**
 * Whether this element's own box is transitioned.
 *
 * A layer effect is rendered into a texture sized to the element, and the texture is thrown away and
 * rebuilt whenever that size changes. A `transition: width` therefore regenerates it every frame the
 * transition runs - a push, a render, a readback and a new texture, for a shadow that did not change
 * shape. It is the most expensive thing a small effect can be attached to.
 */
bool ResizesItself(const Rml::ComputedValues& computed)
{
	const Rml::TransitionList* transitions = computed.transition();
	if (!transitions || transitions->none)
		return false;

	if (transitions->all)
		return true;

	for (const Rml::Transition& transition : transitions->transitions)
	{
		switch (transition.id)
		{
		case Rml::PropertyId::Width:
		case Rml::PropertyId::Height:
		case Rml::PropertyId::MinWidth:
		case Rml::PropertyId::MaxWidth:
		case Rml::PropertyId::MinHeight:
		case Rml::PropertyId::MaxHeight:
		case Rml::PropertyId::PaddingTop:
		case Rml::PropertyId::PaddingRight:
		case Rml::PropertyId::PaddingBottom:
		case Rml::PropertyId::PaddingLeft:
		case Rml::PropertyId::BorderTopWidth:
		case Rml::PropertyId::BorderRightWidth:
		case Rml::PropertyId::BorderBottomWidth:
		case Rml::PropertyId::BorderLeftWidth: return true;
		default: break;
		}
	}

	return false;
}

void Walk(Rml::Element* element, const Rml::Vector2f viewport, std::vector<Finding>& out)
{
	if (!element)
		return;

	if (element->IsVisible())
	{
		const Rml::ComputedValues& computed = element->GetComputedValues();
		const std::string properties = LayerProperties(computed);

		if (!properties.empty() && viewport.x > 0.0f && viewport.y > 0.0f)
		{
			const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
			const Rml::AnimationList* animation = computed.animation();

			out.push_back({ComputeElementPath(element), properties,
				(size.x * size.y) / (viewport.x * viewport.y),
				animation && !animation->empty(), ResizesItself(computed)});
		}
	}

	for (int i = 0; i < element->GetNumChildren(); ++i)
		Walk(element->GetChild(i), viewport, out);
}

std::string Percent(const float coverage)
{
	return std::to_string(int(std::lround(coverage * 100.0f))) + "%";
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

	if (findings.empty())
		return;

	for (const Finding& finding : findings)
	{
		// The worst of the three, and only one is reported: a resizing shadow is already the whole
		// story about that element, and saying it covers the screen as well does not change what to
		// do about it.
		if (finding.resizes)
		{
			diagnostics.Report(Severity::Warning, "W-PERF-RESIZINGLAYER",
				finding.path + ": " + finding.properties + " on an element whose box is transitioned. "
				"The effect is rendered into a texture sized to the element, so every frame of that "
				"transition throws it away and builds another - a pass, a readback and a new texture, "
				"for a shape that did not change. Transition transform: scaleX() on a fixed box "
				"instead, or bake the effect and animate the texture.",
				options.document, 0,
				{{"path", finding.path}, {"properties", finding.properties}, {"resizes", true}});
			continue;
		}

		if (finding.coverage >= FullScreenArea)
		{
			std::string message = finding.path + ": " + finding.properties + " on an element covering "
				+ Percent(finding.coverage) + " of the viewport costs a full-screen render target";
			message += finding.animated
				? ", and it is animating, so that is one every frame"
				: ", every frame the document draws";
			message += ". Worth knowing rather than worth fixing on its own: check stat Folio before "
				"reaching for folio.Bake, and mind that a baked full-screen texture is its own cost "
				"on the game thread the frame it first draws.";

			// Info rather than a warning. A vignette or a scrim built the obvious way is a legitimate
			// document, the replay behind it is no longer what it was, and a texture is not always
			// the cheaper answer - so this reports and does not fail a build.
			diagnostics.Report(Severity::Info, "I-PERF-FULLSCREENLAYER", std::move(message),
				options.document, 0,
				{{"path", finding.path}, {"properties", finding.properties},
					{"coverage", int(std::lround(finding.coverage * 100.0f))},
					{"animated", finding.animated}});
		}
	}

	// Counted as well as measured, because size is not what a layer costs. Each one is its own
	// offscreen pass - a render-target bind, a draw and a composite - and two 236x50dp drop shadows
	// measured heavier than the full-screen vignette beside them. A document that never idles pays
	// for all of them every frame.
	if ((int)findings.size() >= CrowdedLayerCount)
	{
		std::string names;
		for (const Finding& finding : findings)
		{
			if (!names.empty())
				names += ", ";
			names += finding.path;
		}

		// Info rather than a warning: a document demonstrating masks legitimately has seven, and
		// nothing here can tell that from a HUD that cannot afford one. The two checks above are the
		// ones that name a specific, fixable element.
		diagnostics.Report(Severity::Info, "I-PERF-LAYERCOUNT",
			std::to_string(findings.size()) + " elements draw a layer effect, and each is its own "
			"offscreen pass whatever its size: " + names + ". A hard-edged shadow, a solid glow or a "
			"mask that never changes is a texture - bake it. Only an effect that reads what is behind "
			"it has to stay a layer.",
			options.document, 0, {{"count", (int)findings.size()}});
	}
}

} // namespace uipreview

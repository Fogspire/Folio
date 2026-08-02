#include "AccessibilityReport.h"

#include "App.h"
#include "ColourVision.h"
#include "Diagnostics.h"
#include "ElementPath.h"
#include "Options.h"

#include "FolioCore/FolioSemantics.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Property.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

using ordered_json = nlohmann::ordered_json;

namespace uipreview {

namespace {

double Round2(double value)
{
	return std::round(value * 100.0) / 100.0;
}

/**
 * WCAG 2.1 relative luminance.
 *
 * The channel transfer is sRGB's, not a gamma of 2.2, because the ratios everyone quotes are defined
 * against this curve - approximating it moves borderline pairs across the threshold in both
 * directions, which is worse than not checking.
 */
double Luminance(Rml::Colourb colour)
{
	const auto channel = [](unsigned char value) {
		const double v = value / 255.0;
		return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
	};

	return 0.2126 * channel(colour.red) + 0.7152 * channel(colour.green) + 0.0722 * channel(colour.blue);
}

double ContrastRatio(Rml::Colourb a, Rml::Colourb b)
{
	const double la = Luminance(a);
	const double lb = Luminance(b);
	return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/** Source over, both operands straight rather than premultiplied. */
Rml::Colourb Over(Rml::Colourb source, Rml::Colourb backdrop)
{
	const double alpha = source.alpha / 255.0;
	const auto mix = [alpha](unsigned char s, unsigned char b) {
		return (unsigned char)std::lround(s * alpha + b * (1.0 - alpha));
	};

	return Rml::Colourb(mix(source.red, backdrop.red), mix(source.green, backdrop.green),
		mix(source.blue, backdrop.blue), 255);
}

/**
 * What is actually behind an element's text.
 *
 * Walks out to the nearest ancestor with an opaque background and composites everything between back
 * down, because a translucent panel over a dark scene is a different contrast to the same panel over
 * a light one and taking the element's own background alone would be wrong in both cases.
 *
 * Returns false when nothing in the chain is opaque - a document meant to be drawn over the world has
 * no knowable backdrop, and guessing one would produce confident nonsense.
 */
bool Covers(Rml::Element* candidate, Rml::Element* element)
{
	const Rml::Vector2f outer = candidate->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f outer_size = candidate->GetBox().GetSize(Rml::BoxArea::Border);
	const Rml::Vector2f inner = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f inner_size = element->GetBox().GetSize(Rml::BoxArea::Border);

	return outer.x <= inner.x && outer.y <= inner.y
		&& outer.x + outer_size.x >= inner.x + inner_size.x
		&& outer.y + outer_size.y >= inner.y + inner_size.y;
}

/**
 * An earlier sibling that is drawn behind this one and covers it.
 *
 * The component library builds a segmented control as an absolutely positioned hit area followed by
 * its label, so the coloured chip behind the text is a sibling rather than an ancestor. Walking only
 * ancestors reported that text against the panel it is nowhere near, which is a confident wrong number
 * - worse than no number.
 */
Rml::Element* CoveringSibling(Rml::Element* at, Rml::Element* element)
{
	for (Rml::Element* previous = at->GetPreviousSibling(); previous;
		previous = previous->GetPreviousSibling())
	{
		if (previous->GetComputedValues().background_color().alpha == 255 && Covers(previous, element))
			return previous;
	}

	return nullptr;
}

bool ResolveBackdrop(Rml::Element* element, Rml::Colourb& out_colour)
{
	// Nearest layer first. Composited bottom up once something opaque closes the stack.
	std::vector<Rml::Colourb> stack;

	const auto close = [&stack, &out_colour](Rml::Colourb opaque) {
		Rml::Colourb result = opaque;
		for (auto layer = stack.rbegin(); layer != stack.rend(); ++layer)
			result = Over(*layer, result);

		out_colour = result;
		return true;
	};

	for (Rml::Element* at = element; at; at = at->GetParentNode())
	{
		const Rml::Colourb background = at->GetComputedValues().background_color();
		if (background.alpha == 255)
			return close(background);

		if (background.alpha > 0)
			stack.push_back(background);

		if (Rml::Element* behind = CoveringSibling(at, element))
			return close(behind->GetComputedValues().background_color());
	}

	return false;
}

// Naming, interactivity and what counts as decoration all come from the semantic walk the engine
// builds its accessibility tree with, so a control the checker calls named is a control a screen
// reader will read out. See FolioCore/FolioSemantics.h.
using Folio::AccessibleName;
using Folio::IsInteractive;
using Folio::OwnText;

/** Properties whose change between rest and focus a person could see. */
const Rml::PropertyId kFocusVisible[] = {
	Rml::PropertyId::Color,
	Rml::PropertyId::BackgroundColor,
	Rml::PropertyId::BorderTopColor,
	Rml::PropertyId::BorderRightColor,
	Rml::PropertyId::BorderBottomColor,
	Rml::PropertyId::BorderLeftColor,
	Rml::PropertyId::BorderTopWidth,
	Rml::PropertyId::BorderRightWidth,
	Rml::PropertyId::BorderBottomWidth,
	Rml::PropertyId::BorderLeftWidth,
	Rml::PropertyId::Opacity,
	Rml::PropertyId::Decorator,
	Rml::PropertyId::Transform,
	Rml::PropertyId::BoxShadow,
	Rml::PropertyId::Filter,
	Rml::PropertyId::FontEffect,
	Rml::PropertyId::ImageColor,
};

std::string Snapshot(Rml::Element* element)
{
	std::string joined;
	for (const Rml::PropertyId id : kFocusVisible)
	{
		if (const Rml::Property* property = element->GetProperty(id))
			joined += property->ToString();
		joined += "|";
	}

	return joined;
}

/**
 * Whether focusing this element changes how it looks.
 *
 * Asked by actually setting the pseudo class and re-resolving, rather than by reading the stylesheet:
 * a focus style can arrive from any rule reaching the element, including one on an ancestor with a
 * descendant selector, and reimplementing the cascade to find out would be both large and wrong.
 *
 * The whole document is compared rather than the element's own subtree, because a focus treatment is
 * regularly written on something else: `.radio:focus-visible + .radio-label` puts it on the sibling,
 * and a checkbox's entire visible response lives there. Comparing the subtree alone reported every
 * one of those as unstyled.
 */
bool FocusIsVisible(Rml::Element* element, Rml::ElementDocument* document)
{
	std::vector<Rml::Element*> subtree;
	std::vector<Rml::Element*> queue{document};

	while (!queue.empty())
	{
		Rml::Element* at = queue.back();
		queue.pop_back();
		subtree.push_back(at);

		for (int i = 0; i < at->GetNumChildren(); ++i)
			queue.push_back(at->GetChild(i));
	}

	const bool was_focus = element->IsPseudoClassSet("focus");
	const bool was_visible = element->IsPseudoClassSet("focus-visible");

	// The unfocused state is established rather than assumed. An element with autofocus is already
	// focused when the walk reaches it, so snapshotting as-is and then focusing compares the focused
	// state against itself and reports the one control the author most cared about as unstyled.
	element->SetPseudoClass("focus", false);
	element->SetPseudoClass("focus-visible", false);
	document->UpdateDocument();

	std::vector<std::string> before;
	before.reserve(subtree.size());
	for (Rml::Element* at : subtree)
		before.push_back(Snapshot(at));

	// Both, because a focus ring should only show for keyboard and gamepad and so the library styles
	// :focus-visible - checking :focus alone would report every correctly styled control in it.
	element->SetPseudoClass("focus", true);
	element->SetPseudoClass("focus-visible", true);
	document->UpdateDocument();

	bool changed = false;
	for (size_t i = 0; i < subtree.size(); ++i)
	{
		if (Snapshot(subtree[i]) != before[i])
		{
			changed = true;
			break;
		}
	}

	element->SetPseudoClass("focus", was_focus);
	element->SetPseudoClass("focus-visible", was_visible);
	document->UpdateDocument();

	return changed;
}

struct Finding {
	std::string code;
	std::string path;
	std::string message;
	ordered_json data;
	/** Warning unless the finding is a judgement rather than a defect, which only one of them is. */
	Severity severity = Severity::Warning;
};

/**
 * Large text is allowed a lower ratio because it is easier to read at the same contrast. The
 * thresholds are WCAG's, in CSS px: 24, or 18.66 when bold.
 */
bool IsLargeText(const Rml::ComputedValues& computed, float dp_ratio)
{
	const float px = computed.font_size() / std::max(0.01f, dp_ratio);
	const bool bold = (int)computed.font_weight() >= 700;
	return px >= 24.0f || (bold && px >= 18.66f);
}

/**
 * Whether the element's own box is smaller than what is inside it, and cuts it off.
 *
 * Only where `overflow` is hidden, because that is the difference between text that has been lost and
 * a panel that scrolls. The two-pixel allowance is for the rounding between a laid-out box and the
 * text inside it, which can disagree by a fraction at any size.
 */
bool IsClipped(Rml::Element* element, float& out_over_x, float& out_over_y)
{
	const Rml::ComputedValues& computed = element->GetComputedValues();

	const bool clips_x = computed.overflow_x() == Rml::Style::Overflow::Hidden;
	const bool clips_y = computed.overflow_y() == Rml::Style::Overflow::Hidden;
	if (!clips_x && !clips_y)
		return false;

	out_over_x = clips_x ? element->GetScrollWidth() - element->GetClientWidth() : 0.0f;
	out_over_y = clips_y ? element->GetScrollHeight() - element->GetClientHeight() : 0.0f;

	return out_over_x > 2.0f || out_over_y > 2.0f;
}

/** Whether anything in the document takes focus at all, which is what makes it a screen rather than a HUD. */
bool IsNavigable(Rml::Element* element)
{
	if (element->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto)
		return true;

	for (int i = 0; i < element->GetNumChildren(); ++i)
	{
		if (IsNavigable(element->GetChild(i)))
			return true;
	}

	return false;
}

void Walk(Rml::Element* element, Rml::ElementDocument* document, const Options& options, float dp_ratio,
	const std::map<Rml::Element*, float>& unscaled_text, bool out_navigable, std::vector<Finding>& out,
	bool& out_backdrop_unknown)
{
	if (!element)
		return;

	// An author saying this is decoration is the end of the conversation: a spacer, a rule, a glow.
	if (Folio::IsHiddenFromAccessibility(element))
		return;

	const Rml::ComputedValues& computed = element->GetComputedValues();
	const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
	const bool visible = Folio::IsVisibleForAccessibility(element);

	// A control at zero opacity over its own hit area is the styled-checkbox pattern rather than a
	// hidden control, and it is still what a click hits and what focus lands on. So the control checks
	// run on it, while contrast and text size - which are about what is on screen - do not.
	const bool reachable = Folio::IsInAccessibilityTree(element) && !Folio::IsInert(element);

	{
		const std::string path = ComputeElementPath(element);
		const std::string own = OwnText(element);

		if (visible && !own.empty())
		{
			const float px = computed.font_size() / std::max(0.01f, dp_ratio);
			if (px < options.a11y_min_text)
			{
				out.push_back({"W-A11Y-TEXTSIZE", path,
					"text is " + std::to_string((int)std::lround(px)) + "dp, under the "
						+ std::to_string((int)std::lround(options.a11y_min_text)) + "dp floor",
					{{"size", Round2(px)}, {"floor", Round2(options.a11y_min_text)}, {"text", own}}});
			}

			Rml::Colourb backdrop;
			if (ResolveBackdrop(element, backdrop))
			{
				const Rml::Colourb text = Over(computed.color(), backdrop);
				const double ratio = ContrastRatio(text, backdrop);
				const double wanted = IsLargeText(computed, dp_ratio) ? 3.0 : options.a11y_contrast;

				if (ratio < wanted)
				{
					out.push_back({"W-A11Y-CONTRAST", path,
						"text contrast is " + std::to_string(Round2(ratio)).substr(0, 4) + ":1, under "
							+ std::to_string(Round2(wanted)).substr(0, 4) + ":1",
						{{"ratio", Round2(ratio)}, {"required", Round2(wanted)}, {"text", own}}});
				}
				else if (options.simulate != ColourVision::Normal)
				{
					// Only where it passes as drawn and fails as seen. Anything failing both is already
					// reported above, and saying it twice would bury this one.
					const double seen = ContrastRatio(SimulateColour(text, options.simulate),
						SimulateColour(backdrop, options.simulate));

					if (seen < wanted)
					{
						out.push_back({"W-A11Y-CVD", path,
							std::string("text contrast is ") + std::to_string(Round2(seen)).substr(0, 4)
								+ ":1 to " + ColourVisionName(options.simulate) + ", under "
								+ std::to_string(Round2(wanted)).substr(0, 4)
								+ ":1 - it passes only on a hue difference that vision does not have",
							{{"ratio", Round2(seen)}, {"drawnRatio", Round2(ratio)},
								{"required", Round2(wanted)},
								{"vision", ColourVisionName(options.simulate)}, {"text", own}}});
					}
				}
			}
			else
			{
				out_backdrop_unknown = true;
			}

			// Text the scale could not move. Measured rather than read off the unit, because a size can
			// arrive by inheritance from an ancestor several levels up and the only question that
			// matters is whether this text actually moved.
			if (const auto found = unscaled_text.find(element); found != unscaled_text.end())
			{
				if (std::abs(computed.font_size() - found->second) < 0.5f)
				{
					// Information rather than a warning, because some text is right not to move: a
					// letter drawn inside a key cap, a logo, a version string. What this answers is
					// "which text did the setting reach", which is a question only the author can
					// finish.
					out.push_back({"I-A11Y-FIXEDTEXT", path,
						"this text does not move with the text scale - size it in rem rather than dp if "
						"it should",
						{{"size", Round2(computed.font_size() / std::max(0.01f, dp_ratio))},
							{"text", own}},
						Severity::Info});
				}
			}
		}

		if (float over_x = 0.0f, over_y = 0.0f; IsClipped(element, over_x, over_y))
		{
			out.push_back({"W-A11Y-CLIPPED", ComputeElementPath(element),
				"content is cut off: " + std::to_string((int)std::lround(over_x)) + "x"
					+ std::to_string((int)std::lround(over_y)) + "px of it overflows a box that hides "
					"what does not fit",
				{{"overflowX", Round2(over_x)}, {"overflowY", Round2(over_y)}}});
		}

		if (reachable && IsInteractive(element))
		{
			// A control the pointer can press and navigation cannot reach. Only asked on a document
			// that navigates at all: a HUD with nothing focusable is not a screen anyone walks, and
			// reporting every clickable thing in one would train people to ignore this.
			if (out_navigable && computed.tab_index() != Rml::Style::TabIndex::Auto)
			{
				out.push_back({"W-A11Y-UNREACHABLE", ComputeElementPath(element),
					"only a pointer can reach this control - give it tab-index: auto, or hide it from "
					"the tree if it is decoration",
					{{"tag", element->GetTagName()}}});
			}

			if (AccessibleName(element).empty())
			{
				out.push_back({"W-A11Y-NONAME", path,
					"nothing names this control - give it folio-label",
					{{"tag", element->GetTagName()}}});
			}

			const float width = size.x / std::max(0.01f, dp_ratio);
			const float height = size.y / std::max(0.01f, dp_ratio);
			if (width < options.a11y_min_target || height < options.a11y_min_target)
			{
				out.push_back({"W-A11Y-TARGET", path,
					"hit target is " + std::to_string((int)std::lround(width)) + "x"
						+ std::to_string((int)std::lround(height)) + "dp, under "
						+ std::to_string((int)std::lround(options.a11y_min_target)) + "dp",
					{{"width", Round2(width)}, {"height", Round2(height)},
						{"floor", Round2(options.a11y_min_target)}}});
			}

			if (computed.tab_index() == Rml::Style::TabIndex::Auto && !FocusIsVisible(element, document))
			{
				out.push_back({"W-A11Y-NOFOCUS", path,
					"focusing this changes nothing visible, so keyboard and gamepad navigation is "
					"invisible on it",
					{{"tag", element->GetTagName()}}});
			}
		}
	}

	for (int i = 0; i < element->GetNumChildren(); ++i)
		Walk(element->GetChild(i), document, options, dp_ratio, unscaled_text, out_navigable, out,
			out_backdrop_unknown);
}

/**
 * Turns transitions off across the document, and back on.
 *
 * The focus check would otherwise be wrong on every control that animates into its focus state, which
 * is all of them in the shipped library: setting the pseudo class starts a transition rather than
 * applying the new value, so the computed value read straight afterwards is still the old one and a
 * correctly styled control reads as having no focus style at all.
 */
void SuppressTransitions(Rml::Element* element, bool suppress)
{
	if (!element)
		return;

	if (suppress)
		element->SetProperty("transition", "none");
	else
		element->RemoveProperty("transition");

	for (int i = 0; i < element->GetNumChildren(); ++i)
		SuppressTransitions(element->GetChild(i), suppress);
}

/**
 * Every text size as it would be with the scale taken back off.
 *
 * Asked by removing the root size and re-resolving, which is the same trick the focus check uses and
 * for the same reason: the alternative is to reimplement the cascade and be wrong about inheritance.
 * Empty when nothing was scaled, and then the check it feeds never fires.
 */
std::map<Rml::Element*, float> MeasureUnscaledText(Rml::ElementDocument* document, const Options& options)
{
	std::map<Rml::Element*, float> sizes;
	if (options.text_scale == 1.0f)
		return sizes;

	const Rml::Property* scaled = document->GetLocalProperty(Rml::PropertyId::FontSize);
	if (!scaled)
		return sizes;

	const Rml::Property saved = *scaled;
	document->RemoveProperty(Rml::PropertyId::FontSize);
	document->UpdateDocument();

	std::vector<Rml::Element*> queue{document};
	while (!queue.empty())
	{
		Rml::Element* at = queue.back();
		queue.pop_back();

		sizes[at] = at->GetComputedValues().font_size();

		for (int i = 0; i < at->GetNumChildren(); ++i)
			queue.push_back(at->GetChild(i));
	}

	document->SetProperty(Rml::PropertyId::FontSize, saved);
	document->UpdateDocument();

	return sizes;
}

std::vector<Finding> Collect(const Options& options, App& app, bool& out_backdrop_unknown)
{
	std::vector<Finding> findings;

	Rml::ElementDocument* document = app.GetDocument();
	Rml::Context* context = app.GetContext();
	if (!document || !context)
		return findings;

	SuppressTransitions(document, true);
	document->UpdateDocument();

	const std::map<Rml::Element*, float> unscaled = MeasureUnscaledText(document, options);

	Walk(document, document, options, context->GetDensityIndependentPixelRatio(), unscaled,
		IsNavigable(document), findings, out_backdrop_unknown);

	SuppressTransitions(document, false);
	document->UpdateDocument();

	return findings;
}

} // namespace

void CheckAccessibility(const Options& options, App& app, Diagnostics& diagnostics)
{
	bool backdrop_unknown = false;
	const std::vector<Finding> findings = Collect(options, app, backdrop_unknown);

	for (const Finding& finding : findings)
	{
		ordered_json data = finding.data;
		data["path"] = finding.path;
		diagnostics.Report(finding.severity, finding.code, finding.path + ": " + finding.message,
			options.document, 0, std::move(data));
	}

	if (backdrop_unknown)
	{
		// Info rather than a warning: a document drawn over the world is supposed to have no backdrop,
		// and this is the only honest thing to say about contrast on one.
		diagnostics.Report(Severity::Info, "I-A11Y-NOBACKDROP",
			"some text has no opaque background above it, so its contrast could not be checked - "
			"judge those against the scene they are drawn over",
			options.document);
	}
}

namespace {

ordered_json TreeToJson(const Folio::FSemanticNode& node, float dp_ratio)
{
	const float scale = std::max(0.01f, dp_ratio);

	ordered_json entry;
	entry["role"] = Folio::RoleName(node.Role);
	entry["path"] = ComputeElementPath(node.Element);
	entry["name"] = node.Name;

	if (!node.Description.empty())
		entry["description"] = node.Description;

	if (!node.Value.empty())
		entry["value"] = node.Value;

	if (node.SetSize > 1)
	{
		entry["position"] = node.Position;
		entry["setSize"] = node.SetSize;
	}

	if (!node.Column.empty())
		entry["column"] = node.Column;

	if (node.Interactive)
		entry["interactive"] = true;

	if (node.Disabled)
		entry["disabled"] = true;

	if (node.Checkable)
		entry["checked"] = node.Checked;

	if (node.HasRange)
	{
		entry["minimum"] = Round2(node.Minimum);
		entry["maximum"] = Round2(node.Maximum);
		if (node.Step != 0.0f)
			entry["step"] = Round2(node.Step);
	}

	entry["bounds"] = {
		{"x", Round2(node.Offset.x / scale)},
		{"y", Round2(node.Offset.y / scale)},
		{"width", Round2(node.Size.x / scale)},
		{"height", Round2(node.Size.y / scale)},
	};

	if (!node.Children.empty())
	{
		ordered_json children = ordered_json::array();
		for (const Folio::FSemanticNode& child : node.Children)
			children.push_back(TreeToJson(child, dp_ratio));

		entry["children"] = std::move(children);
	}

	return entry;
}

} // namespace

bool WriteAccessibilityTree(const std::string& path, const Options& options, App& app,
	std::string& out_error)
{
	Rml::ElementDocument* document = app.GetDocument();
	Rml::Context* context = app.GetContext();
	if (!document || !context)
	{
		out_error = "no document";
		return false;
	}

	const float dp_ratio = context->GetDensityIndependentPixelRatio();

	ordered_json report;
	report["schema"] = 1;
	report["tool"] = {{"name", "uipreview"}};
	report["command"] = {{"document", options.document}};
	report["dpRatio"] = Round2(dp_ratio);

	int nodes = 0;
	int unnamed = 0;
	const Folio::FSemanticNode root = Folio::BuildSemanticTree(document);
	Folio::VisitSemanticTree(root, [&nodes, &unnamed](const Folio::FSemanticNode& node, int) {
		++nodes;
		if (node.Interactive && node.Name.empty())
			++unnamed;
	});

	report["totals"] = {{"nodes", nodes}, {"unnamedControls", unnamed}};
	report["tree"] = TreeToJson(root, dp_ratio);

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		out_error = "could not open for writing";
		return false;
	}

	file << report.dump(2) << "\n";
	return true;
}

void PrintAccessibilityTree(App& app, std::FILE* stream)
{
	Rml::ElementDocument* document = app.GetDocument();
	if (!document)
		return;

	const Folio::FSemanticNode root = Folio::BuildSemanticTree(document);

	Folio::VisitSemanticTree(root, [stream](const Folio::FSemanticNode& node, const int depth) {
		std::string line(size_t(depth) * 2, ' ');
		line += Folio::RoleName(node.Role);

		if (!node.Column.empty())
			line += " [" + node.Column + "]";

		if (!node.Name.empty())
			line += " \"" + node.Name + "\"";
		else if (node.Interactive)
			line += " <unnamed>";

		if (node.SetSize > 1)
			line += " " + std::to_string(node.Position) + "/" + std::to_string(node.SetSize);

		if (!node.Value.empty() && !Folio::RoleSaysItsOwnValue(node.Role))
			line += " = " + node.Value;

		if (node.Checkable)
			line += node.Checked ? " [checked]" : " [unchecked]";

		if (node.Disabled)
			line += " [disabled]";

		std::fprintf(stream, "%s\n", line.c_str());
	});
}

bool WriteAccessibilityReport(const std::string& path, const Options& options, App& app,
	Diagnostics& diagnostics, std::string& out_error)
{
	(void)diagnostics;

	bool backdrop_unknown = false;
	const std::vector<Finding> findings = Collect(options, app, backdrop_unknown);

	ordered_json report;
	report["schema"] = 1;
	report["tool"] = {{"name", "uipreview"}};
	report["command"] = {{"document", options.document}};
	report["thresholds"] = {
		{"contrast", Round2(options.a11y_contrast)},
		{"minText", Round2(options.a11y_min_text)},
		{"minTarget", Round2(options.a11y_min_target)},
	};
	report["backdropUnknown"] = backdrop_unknown;

	ordered_json entries = ordered_json::array();
	for (const Finding& finding : findings)
	{
		ordered_json entry = finding.data;
		entry["code"] = finding.code;
		entry["path"] = finding.path;
		entry["message"] = finding.message;
		entries.push_back(std::move(entry));
	}

	report["totals"] = {{"findings", (int)entries.size()}};
	report["findings"] = std::move(entries);

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

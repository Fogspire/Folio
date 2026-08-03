#include "Bake.h"

#include "App.h"
#include "Diagnostics.h"
#include "Options.h"
#include "Screenshot.h"

#include "backend/UipreviewBackendGLFW.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <vector>

namespace uipreview {
namespace {

/** Every inline property a bake sets, so one pass can undo all of it. */
struct Applied {
	Rml::Element* element = nullptr;
	const char* property = nullptr;
};

/*
 * What an ancestor of the baked element has to stop doing.
 *
 * It cannot simply be hidden. RmlUi drops an invisible element from the stacking context along with
 * everything under it, so hiding the body hides the thing being baked - which is why this is a list
 * of what an ancestor paints rather than one `visibility` on the root.
 */
constexpr const char* AncestorSilenced[] = {"background-color", "decorator", "box-shadow", "filter",
	"backdrop-filter", "mask-image", "border-width", "opacity"};
constexpr const char* AncestorValues[] = {"transparent", "none", "none", "none", "none", "none", "0dp",
	"1"};

void Set(Rml::Element* element, const char* property, const char* value, std::vector<Applied>& applied)
{
	element->SetProperty(property, value);
	applied.push_back({element, property});
}

void Isolate(Rml::Element* element, Rml::Element* keep, const std::set<Rml::Element*>& ancestors,
	std::vector<Applied>& applied)
{
	if (!element)
		return;

	if (element == keep)
	{
		// At full strength whatever the document rests at. A layer that is faded out until the game
		// turns it on would otherwise bake to nothing, and opacity is what the element drawing the
		// texture animates anyway.
		Set(element, "opacity", "1", applied);
		return;
	}

	if (ancestors.count(element) != 0)
	{
		for (size_t i = 0; i < sizeof(AncestorSilenced) / sizeof(AncestorSilenced[0]); ++i)
			Set(element, AncestorSilenced[i], AncestorValues[i], applied);

		for (int i = 0; i < element->GetNumChildren(); ++i)
			Isolate(element->GetChild(i), keep, ancestors, applied);

		return;
	}

	Set(element, "visibility", "hidden", applied);
}

void Restore(const std::vector<Applied>& applied)
{
	for (const Applied& one : applied)
		one.element->RemoveProperty(one.property);
}

/**
 * Straight alpha, because Folio premultiplies a file texture as it loads.
 *
 * RmlUi composites premultiplied and the framebuffer is read back the same way, so writing it as it
 * stands would have the engine multiply by alpha a second time. The symptom is a wash that looks
 * right where it is opaque and too dark everywhere it is not, which reads as the colour being wrong
 * rather than the encoding.
 */
void Unpremultiply(std::vector<unsigned char>& rgba)
{
	for (size_t i = 0; i + 3 < rgba.size(); i += 4)
	{
		const unsigned alpha = rgba[i + 3];
		if (alpha == 0 || alpha == 255)
			continue;

		for (int channel = 0; channel < 3; ++channel)
		{
			const unsigned value = (rgba[i + channel] * 255u + alpha / 2) / alpha;
			rgba[i + channel] = (unsigned char)std::min(255u, value);
		}
	}
}

/** The element's border box in framebuffer pixels, grown by the margin and clipped to the frame. */
bool CropRect(Rml::Element* element, int frame_width, int frame_height, int margin, int& out_x, int& out_y,
	int& out_width, int& out_height)
{
	const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);

	const int left = (int)std::floor(offset.x) - margin;
	const int top = (int)std::floor(offset.y) - margin;
	const int right = (int)std::ceil(offset.x + size.x) + margin;
	const int bottom = (int)std::ceil(offset.y + size.y) + margin;

	out_x = std::max(0, left);
	out_y = std::max(0, top);
	out_width = std::min(frame_width, right) - out_x;
	out_height = std::min(frame_height, bottom) - out_y;

	return out_width > 0 && out_height > 0;
}

void Crop(const std::vector<unsigned char>& source, int source_width, int x, int y, int width, int height,
	std::vector<unsigned char>& out)
{
	out.resize((size_t)width * (size_t)height * 4);
	for (int row = 0; row < height; ++row)
	{
		const size_t from = ((size_t)(y + row) * (size_t)source_width + (size_t)x) * 4;
		std::memcpy(&out[(size_t)row * (size_t)width * 4], &source[from], (size_t)width * 4);
	}
}

} // namespace

bool ParseBakeArgument(const std::string& argument, std::string& out_selector, std::string& out_path)
{
	const size_t split = argument.find_last_of('=');
	if (split == std::string::npos || split == 0 || split + 1 >= argument.size())
		return false;

	out_selector = argument.substr(0, split);
	out_path = argument.substr(split + 1);
	return true;
}

bool RunBakes(const Options& options, App& app, Diagnostics& diagnostics)
{
	Rml::ElementDocument* document = app.GetDocument();
	Rml::Context* context = app.GetContext();
	if (!document || !context)
		return false;

	const Rml::Vector2i dimensions = context->GetDimensions();
	bool ok = true;

	for (const std::string& request : options.bakes)
	{
		std::string selector;
		std::string path;
		if (!ParseBakeArgument(request, selector, path))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-ARGUMENT",
				"--bake wants <selector>=<out.png>, not '" + request + "'", options.document);
			ok = false;
			continue;
		}

		Rml::Element* element = document->QuerySelector(selector);
		if (!element)
		{
			diagnostics.Report(Severity::Error, "E-BAKE-SELECTOR",
				"nothing matches '" + selector + "'", options.document);
			ok = false;
			continue;
		}

		std::set<Rml::Element*> ancestors;
		for (Rml::Element* up = element->GetParentNode(); up; up = up->GetParentNode())
			ancestors.insert(up);

		std::vector<Applied> applied;
		Isolate(document, element, ancestors, applied);

		// Twice: the property change is a style dirty, and the frame after it is the one that has the
		// isolated tree in it.
		context->Update();
		context->Update();

		Backend::SetTransparentClear(true);
		Backend::BeginFrame();
		context->Render();
		Backend::EndFrame();
		Backend::SetTransparentClear(false);

		std::vector<unsigned char> frame;
		std::string error;
		if (!CaptureBackBuffer(dimensions.x, dimensions.y, frame, error))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-CAPTURE", selector + ": " + error, options.document);
			Restore(applied);
			ok = false;
			continue;
		}

		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
		if (!CropRect(element, dimensions.x, dimensions.y, options.bake_margin, x, y, width, height))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-EMPTY",
				selector + ": lays out to nothing on screen, so there is nothing to bake",
				options.document);
			Restore(applied);
			ok = false;
			continue;
		}

		std::vector<unsigned char> cropped;
		Crop(frame, dimensions.x, x, y, width, height, cropped);
		Unpremultiply(cropped);

		Restore(applied);
		context->Update();

		if (!WritePng(path, cropped, width, height, error, /*force_rgba*/ true))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-WRITE", path + ": " + error, options.document);
			ok = false;
			continue;
		}

		diagnostics.Report(Severity::Info, "I-BAKE-WROTE",
			selector + " -> " + diagnostics.Relative(path) + " (" + std::to_string(width) + "x"
				+ std::to_string(height) + ")",
			options.document, 0,
			{{"selector", selector}, {"path", diagnostics.Relative(path)}, {"width", width},
				{"height", height}});
	}

	return ok;
}

} // namespace uipreview

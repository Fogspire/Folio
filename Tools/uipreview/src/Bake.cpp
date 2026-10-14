#include "Bake.h"

#include "App.h"
#include "Diagnostics.h"
#include "Options.h"
#include "Screenshot.h"

#include <FolioCore/FolioBake.h>

#include "backend/UipreviewBackendGLFW.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace uipreview {
namespace {

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
		// The search starts below the document, so the document itself is matched by its id here.
		if (!element && !document->GetId().empty() && selector == "#" + document->GetId())
			element = document;
		if (!element)
		{
			diagnostics.Report(Severity::Error, "E-BAKE-SELECTOR",
				"nothing matches '" + selector + "'", options.document);
			ok = false;
			continue;
		}

		// The four properties the two renderers do not draw the same. A texture baked here and drawn
		// in the engine is faint and the wrong shape, and it reads as bad art rather than as a
		// renderer difference - so this refuses rather than producing one.
		if (Folio::Bake::ReadsTheBackdrop(element))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-BACKDROP",
				selector + " uses backdrop-filter, which blurs what is behind it. Baked alone there "
				"is nothing behind it, so the texture would be empty. This is the one effect that has "
				"to stay a layer.", options.document);
			ok = false;
			continue;
		}

		if (Folio::Bake::NeedsEngineBake(element))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-NEEDSENGINE",
				selector + " draws a layer effect - box-shadow, filter, backdrop-filter or "
				"mask-image - which this renderer and the engine do not draw identically. Bake it in "
				"the editor instead: folio.Bake " + options.document + " " + selector + " " + path,
				options.document);
			ok = false;
			continue;
		}

		if (Folio::Bake::HasAnimation(element))
		{
			diagnostics.Report(Severity::Warning, "W-BAKE-ANIMATED",
				selector + " is animating. The animation is cancelled for the bake, which RmlUi does "
				"not always honour - check the result is not empty, and prefer a baker document with "
				"no state classes on its body.", options.document);
		}

		const std::vector<Folio::Bake::FApplied> applied = Folio::Bake::Isolate(document, element);

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
			Folio::Bake::Restore(applied);
			ok = false;
			continue;
		}

		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
		if (!Folio::Bake::CropRect(element, dimensions.x, dimensions.y, options.bake_margin, x, y,
				width, height))
		{
			diagnostics.Report(Severity::Error, "E-BAKE-EMPTY",
				selector + ": lays out to nothing on screen, so there is nothing to bake",
				options.document);
			Folio::Bake::Restore(applied);
			ok = false;
			continue;
		}

		std::vector<unsigned char> cropped;
		Crop(frame, dimensions.x, x, y, width, height, cropped);
		Folio::Bake::Unpremultiply(cropped);

		Folio::Bake::Restore(applied);
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

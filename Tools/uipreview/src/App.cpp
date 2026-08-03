#include "App.h"

#include "ColourVision.h"
#include "DataModel.h"
#include "Diagnostics.h"

#include "ElementPath.h"
#include "FileInterface.h"
#include "FileWatcher.h"
#include "InputScript.h"
#include "Localization.h"
#include "MissingAssets.h"
#include "Paths.h"
#include "Reload.h"
#include "RenderInterface.h"
#include "Screenshot.h"
#include "CoreHost.h"
#include "SystemInterface.h"
#include "TraceReport.h"
#include "backend/UipreviewBackendGLFW.h"

#include "FolioCore/FolioCoreVersion.h"
#include "FolioCore/FolioHost.h"
#include "FolioCore/FolioDecorators.h"
#include "FolioCore/FolioFilters.h"
#include "FolioCore/FolioFontEffects.h"
#include "FolioCore/FolioFontEngine.h"
#include "FolioCore/FolioInputState.h"
#include "FolioCore/FolioTerms.h"
#include "FolioCore/FolioTextElement.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace uipreview {

namespace {

std::string ToForwardSlashes(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');
	return path;
}

bool IsFontFile(const fs::path& path)
{
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return ext == ".ttf" || ext == ".otf";
}

} // namespace

App::App(const Options& in_options, Diagnostics& in_diagnostics) : options(in_options), diagnostics(in_diagnostics) {}

App::~App()
{
	if (rml_initialised)
		Rml::Shutdown();

	// RmlUi holds raw pointers to the instancers, so they only go once it has shut down.
	Folio::ReleaseFolioFontEffects();
	Folio::ReleaseFolioDecorators();
	Folio::ReleaseFolioFilters();

	// Before the backend, which owns the render interface the atlases release their pages
	// through. Left to member order the engine would outlive its own sink and read a freed
	// vtable, which usually survives and occasionally does not.
	font_engine.reset();

	if (backend_initialised)
		Backend::Shutdown();
}

bool App::ResolveRoot()
{
	std::error_code ec;
	const fs::path document_absolute = fs::weakly_canonical(fs::path(options.document), ec);
	if (ec || !fs::exists(document_absolute))
	{
		diagnostics.Report(Severity::Error, "E-DOC-NOTFOUND", "document not found: " + options.document);
		return false;
	}

	if (!options.root.empty())
	{
		root = ToForwardSlashes(fs::weakly_canonical(fs::path(options.root), ec).string());
	}
	else
	{
		// Walk up looking for a marker, so that root-absolute RCSS imports resolve the same
		// way the Unreal file interface will in Phase 3.
		root.clear();
		for (fs::path dir = document_absolute.parent_path(); !dir.empty(); dir = dir.parent_path())
		{
			if (fs::exists(dir / ".folio-root") || fs::is_directory(dir / "UI") || fs::is_directory(dir / "ui"))
			{
				root = ToForwardSlashes(dir.string());
				break;
			}
			if (!dir.has_relative_path())
				break;
		}
		if (root.empty())
			root = ToForwardSlashes(document_absolute.parent_path().string());
	}

	while (!root.empty() && root.back() == '/')
		root.pop_back();

	// Express the document relative to the root, which is the form RmlUi will see.
	const std::string absolute = ToForwardSlashes(document_absolute.string());
	std::string relative = absolute;
	if (absolute.size() > root.size() &&
		std::equal(root.begin(), root.end(), absolute.begin(),
			[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }))
	{
		relative = absolute.substr(root.size());
	}
	if (relative.empty() || relative.front() != '/')
		relative = "/" + relative;
	document_path = relative;

	diagnostics.SetRoot(root);
	diagnostics.SetCurrentDocument(document_path);
	missing_assets.SetDocument(diagnostics.Relative(document_path));
	return true;
}

bool App::Initialise()
{
	if (!ResolveRoot())
		return false;

	Backend::Options backend_options;
	backend_options.window_title = "uipreview";
	backend_options.width = options.width;
	backend_options.height = options.height;
	// A run that renders a fixed number of frames and exits has nothing to show anyone, and a
	// window that appears for a moment steals focus from whatever the person running the harness
	// is actually doing. IsOneShot covers every such mode, so a new one cannot be forgotten here.
	backend_options.visible = !options.IsOneShot();
	backend_options.vsync = backend_options.visible;
	backend_options.resizable = backend_options.visible;

	// The backend owns both interfaces, since they can only be built once the GL context
	// exists. The factories hand back raw pointers so this object can still reach them.
	const bool backend_ok = Backend::Initialize(
		backend_options,
		[this]() {
			auto owned = Rml::MakeUnique<PreviewRenderInterface>(diagnostics, missing_assets);
			render_interface = owned.get();
			return Rml::UniquePtr<RenderInterface_GL3>(std::move(owned));
		},
		[this]() {
			auto owned = Rml::MakeUnique<PreviewSystemInterface>(diagnostics);
			system_interface = owned.get();
			return Rml::UniquePtr<SystemInterface_GLFW>(std::move(owned));
		});

	if (!backend_ok)
	{
		diagnostics.Report(Severity::Error, "E-INIT-BACKEND", "could not create a window or OpenGL 3.3 context");
		return false;
	}
	backend_initialised = true;

	int framebuffer_width = 0, framebuffer_height = 0;
	Backend::GetFramebufferSize(framebuffer_width, framebuffer_height);
	if (framebuffer_width != options.width || framebuffer_height != options.height)
	{
		diagnostics.Report(Severity::Warning, "W-VIEWPORT-SCALED",
			"framebuffer is " + std::to_string(framebuffer_width) + "x" + std::to_string(framebuffer_height) +
				" but " + std::to_string(options.width) + "x" + std::to_string(options.height) + " was requested");
	}

	file_interface = std::make_unique<RootedFileInterface>(root, FolioRoot());
	Rml::SetFileInterface(file_interface.get());
	Rml::SetSystemInterface(Backend::GetSystemInterface());
	Rml::SetRenderInterface(Backend::GetRenderInterface());

	// RmlUi is built with RMLUI_FONT_ENGINE=none, so this is not an upgrade over the built-in
	// engine, it is the only one. Rml::Initialise fails outright if it is missing rather than
	// quietly rendering no text.
	if (options.font_report)
		TraceReport::Install();

	// The core reads faces and reports problems through the host. Here that is a thin wrapper over
	// the same file interface documents use; in Unreal it has to exist because the two modules do
	// not share RmlUi's globals.
	core_host = std::make_unique<PreviewCoreHost>(*file_interface, diagnostics);
	Folio::SetHost(core_host.get());

	Folio::FMsdfAtlasParams msdf_params;
	msdf_params.PxPerEm = options.msdf_px_em;
	msdf_params.PxRange = options.msdf_px_range;
	render_interface->SetMsdfSharpen(options.msdf_sharpen);
	font_engine = std::make_unique<Folio::FFolioFontEngine>(*render_interface, msdf_params);
	font_engine->SetComplexShaping(options.complex_shaping);

	if (options.font_cache)
	{
		font_cache = std::make_unique<FileFontCache>(root);
		font_engine->SetCacheStore(font_cache.get());
	}
	Rml::SetFontEngineInterface(font_engine.get());

	if (!Rml::Initialise())
	{
		diagnostics.Report(Severity::Error, "E-INIT-RMLUI", "Rml::Initialise failed");
		return false;
	}
	rml_initialised = true;

	// After Initialise, because registering over a name replaces an instancer the factory only
	// creates during it. Markup is unchanged; the effects stop being baked into glyph textures and
	// start being resolved from the field.
	Folio::RegisterFolioFontEffects();
	Folio::RegisterFolioFilters();
	Folio::RegisterFolioDecorators();
	Folio::Text::RegisterFolioText();

	context = Rml::CreateContext("main", Rml::Vector2i(framebuffer_width, framebuffer_height));

	// Before any document loads, so nothing renders a frame in the wrong theme. Matches the engine
	// side, which applies the active themes as each context is created.
	for (const std::string& theme : options.themes)
	{
		if (context)
		{
			context->ActivateTheme(theme, true);
		}
	}

	// The same theme the engine's Set Reduced Motion turns on, and the reason it is a theme: a
	// stylesheet is the only thing that reaches a transition an author wrote.
	if (context && options.reduced_motion)
	{
		context->ActivateTheme("reduced-motion", true);

		// Not a transition, so no stylesheet reaches it: RmlUi glides a focused item into view from
		// its own controller, and a list sliding under the thumb is movement like any other.
		context->SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant, 1.0f);
	}

	if (context && options.no_screen_effects)
	{
		context->ActivateTheme("no-screen-effects", true);
	}

	// Its own setting, not part of reduced motion. See docs/accessibility.md.
	Folio::Text::RevealSettings().Speed = options.text_speed;
	Folio::Text::RevealSettings().bInstant = options.instant_text;
	if (!context)
	{
		diagnostics.Report(Severity::Error, "E-INIT-CONTEXT", "could not create the RmlUi context");
		return false;
	}
	ApplyViewportSize(framebuffer_width, framebuffer_height);

	if (!LoadFonts())
		return false;

	// Initialising the debugger adds its beacon element to the context, which shows up as a
	// warning marker in captured images. Keep it out of an unattended capture entirely.
	if (options.debug || !options.IsOneShot())
	{
		Rml::Debugger::Initialise(context);
		Rml::Debugger::SetVisible(options.debug);
	}

	if (!localizer.Load(root, options.culture))
		return false;

	// After the localizer, because a pseudo run inflates the terms as well. A place name that
	// only overflows once it is substituted is exactly the layout this mode is looking for.
	LoadTerms();

	MountInputModel();
	MountDataModel();

	// Data-bound strings never reach the document pass, so inflate them at the source or
	// HUD text escapes pseudo-localization entirely.
	if (localizer.IsPseudo())
	{
		data_model.TransformStrings([this](const std::string& text) { return localizer.Pseudoize(text); });
		for (const std::unique_ptr<JsonDataModel>& model : extra_models)
			model->TransformStrings([this](const std::string& text) { return localizer.Pseudoize(text); });
	}

	return LoadDocumentFromDisk();
}

std::string App::DiscoverDataModelName() const
{
	if (!options.data_model.empty())
		return options.data_model;

	// The model has to exist on the context before the document is loaded, so the DOM cannot
	// be consulted. Scanning the raw markup for the attribute avoids pulling in an XML parser
	// for a single value.
	std::ifstream file(fs::path(root) / fs::path(document_path).relative_path());
	if (!file)
		return {};

	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	static const std::regex pattern(R"RX(data-model\s*=\s*"([^"]+)")RX");
	std::smatch match;
	if (std::regex_search(text, match, pattern))
		return match[1].str();

	return {};
}

std::vector<std::string> App::DiscoverDataModelNames() const
{
	std::vector<std::string> names;

	// An override names one model and means it: it is the document's own, and the rest still come
	// from the markup so a second model is not lost by passing --data-model.
	if (!options.data_model.empty())
		names.push_back(options.data_model);

	std::ifstream file(fs::path(root) / fs::path(document_path).relative_path());
	if (!file)
		return names;

	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	static const std::regex pattern(R"RX(data-model\s*=\s*"([^"]+)")RX");

	for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it)
	{
		const std::string name = (*it)[1].str();
		if (std::find(names.begin(), names.end(), name) == names.end())
			names.push_back(name);
	}

	return names;
}

std::string App::FindConventionalDataFile(const std::string& model_name) const
{
	// A bound document with no data renders its bindings as literal {{ }}, which reads as
	// broken markup rather than as a missing argument. Mock data named after the model is
	// picked up without being asked for, so the common case just works; --data still wins.
	const fs::path candidate = fs::path(UiDirectory(root)) / "mock" / (model_name + ".json");

	std::error_code ec;
	if (!fs::is_regular_file(candidate, ec))
		return {};

	const std::string path = ToForwardSlashes(candidate.string());
	diagnostics.Report(Severity::Info, "I-DATA-MOCK", "using " + diagnostics.Relative(path));
	return path;
}

void App::MountInputModel()
{
	// Always mounted, unlike the game's model: it costs one model per context and a document that
	// never binds it pays nothing, while leaving it out would mean bindings that work in game
	// render as literal braces here.
	Rml::DataModelConstructor constructor = context->CreateDataModel(Folio::InputModel::Name());
	if (!constructor)
		return;

	input_device_value = Folio::DeviceName(options.input_device);
	input_confirm_value = Folio::ConfirmName(options.confirm_button);
	input_back_value = Folio::BackName(options.confirm_button);
	input_is_gamepad = options.input_device == Folio::EInputDevice::Gamepad;

	constructor.Bind(Folio::InputModel::Device(), &input_device_value);
	constructor.Bind(Folio::InputModel::Gamepad(), &input_is_gamepad);
	constructor.Bind(Folio::InputModel::Confirm(), &input_confirm_value);
	constructor.Bind(Folio::InputModel::Back(), &input_back_value);
}

void App::ApplyInputClasses()
{
	Folio::ApplyInputClasses(document, options.input_device, options.confirm_button);
}

void App::MountDataModel()
{
	const std::vector<std::string> model_names = DiscoverDataModelNames();
	const std::string model_name = model_names.empty() ? std::string() : model_names.front();

	// Everything past the first, each from its own mock. --data belongs to the document's own model,
	// so these only ever come from UI/mock/<name>.json.
	extra_models.clear();
	for (std::size_t index = 1; index < model_names.size(); ++index)
	{
		const std::string& name = model_names[index];
		if (name == Folio::InputModel::Name())
			continue;

		auto model = std::make_unique<JsonDataModel>(diagnostics);
		const std::string path = FindConventionalDataFile(name);

		if (path.empty())
		{
			diagnostics.Report(Severity::Warning, "W-DATA-NODATA",
				"the document mounts data-model=\"" + name + "\" but none was found at UI/mock/"
				+ name + ".json");
			model->MountEmpty(context, name);
		}
		else
		{
			model->Mount(context, name, path);
		}

		extra_models.push_back(std::move(model));
	}

	// The reserved model is already mounted and is not a game model, so there is no mock file to
	// look for and nothing missing when there isn't one.
	if (model_name == Folio::InputModel::Name())
		return;

	if (model_name.empty())
	{
		if (!options.data_path.empty())
		{
			diagnostics.Report(Severity::Warning, "W-DATA-NOMODEL",
				"--data was given but the document declares no data-model attribute");
		}
		return;
	}

	data_path = options.data_path.empty() ? FindConventionalDataFile(model_name) : options.data_path;

	if (data_path.empty())
	{
		// Mounting an empty model keeps bindings resolvable, so the document reports one
		// missing data file rather than a warning per bound element.
		diagnostics.Report(Severity::Warning, "W-DATA-NODATA",
			"the document declares data-model=\"" + model_name + "\" but no --data file was given "
			"and none was found at UI/mock/" + model_name + ".json");
		data_model.MountEmpty(context, model_name);
	}
	else
	{
		data_model.Mount(context, model_name, data_path);
	}

	data_model.SetPreviewInfo(options.culture.empty() ? "source" : options.culture, options.width, options.height,
		options.dp_ratio);
}

void App::LoadTerms()
{
	const fs::path terms_dir = fs::path(UiDirectory(root)) / "terms";

	std::vector<fs::path> found;
	std::error_code ec;
	if (fs::is_directory(terms_dir, ec))
	{
		for (const fs::directory_entry& entry : fs::directory_iterator(terms_dir, ec))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".terms")
				found.push_back(entry.path());
		}
	}

	// Sorted, so a duplicate term is always reported against the same one of the two files
	// whichever machine the check runs on.
	std::sort(found.begin(), found.end());

	const std::string ui = ToForwardSlashes(fs::path(UiDirectory(root)).filename().string());
	for (const fs::path& file : found)
	{
		Folio::FTermTables::Get().LoadFile("/" + ui + "/terms/" + ToForwardSlashes(file.filename().string()));
	}

	if (localizer.IsPseudo())
	{
		Folio::FTermTables::Get().ForEach([this](const std::string&, Folio::FTermEntry& entry) {
			entry.Text = localizer.Pseudoize(entry.Text);
		});
	}
}

bool App::LoadFonts()
{
	int loaded = 0;

	// Explicit faces win. Folio bundles no typeface, so this is the supported way to choose
	// one; the directory scan below is only a convenience for a laid-out content root.
	for (const std::string& font : options.fonts)
	{
		if (Rml::LoadFontFace(font))
			++loaded;
		else
			diagnostics.Report(Severity::Error, "E-FONT-LOAD", "could not load font face: " + font);
	}

	const fs::path fonts_dir = fs::path(UiDirectory(root)) / "fonts";
	if (loaded == 0)
	{
		std::vector<fs::path> found;
		std::error_code ec;
		if (fs::is_directory(fonts_dir, ec))
		{
			for (const fs::directory_entry& entry : fs::directory_iterator(fonts_dir, ec))
			{
				if (entry.is_regular_file() && IsFontFile(entry.path()))
					found.push_back(entry.path());
			}
		}
		std::sort(found.begin(), found.end());

		for (const fs::path& font : found)
		{
			if (Rml::LoadFontFace("/" + ToForwardSlashes(fs::path(UiDirectory(root)).filename().string())
					+ "/fonts/" + ToForwardSlashes(font.filename().string())))
				++loaded;
		}
	}

	// Then the typefaces Folio ships. These are variable, so one file covers every weight the
	// markup can ask for, and a game overrides them simply by putting its own under UI/fonts.
	if (loaded == 0)
	{
		const fs::path bundled = fs::path(FolioRoot()) / "fonts";
		std::vector<fs::path> found;
		std::error_code ec;
		if (fs::is_directory(bundled, ec))
		{
			for (const fs::directory_entry& entry : fs::directory_iterator(bundled, ec))
			{
				if (entry.is_regular_file() && IsFontFile(entry.path()))
					found.push_back(entry.path());
			}
		}
		std::sort(found.begin(), found.end());

		// The first one is registered as a fallback, so a document naming a family nobody
		// registered still renders. Rendering nothing at all is the worse failure: it looks like
		// a layout bug and says nothing about the cause.
		for (const fs::path& font : found)
		{
			if (Rml::LoadFontFace(ToForwardSlashes(font.string()), loaded == 0))
				++loaded;
		}
		if (loaded > 0 && options.verbose)
			diagnostics.Report(Severity::Info, "I-FONT-BUNDLED", "using the typefaces bundled with Folio");
	}

	// Finally the engine's Roboto, so a checkout with no bundled fonts still renders.
	if (loaded == 0)
	{
		const std::string engine = FindEngineDirectory(options.engine);
		if (!engine.empty())
		{
			const fs::path slate_fonts = fs::path(engine) / "Content" / "Slate" / "Fonts";
			for (const char* face : {"Roboto-Regular.ttf", "Roboto-Bold.ttf", "Roboto-Italic.ttf", "Roboto-BoldItalic.ttf"})
			{
				const fs::path file = slate_fonts / face;
				std::error_code ec;
				if (fs::exists(file, ec) && Rml::LoadFontFace(ToForwardSlashes(file.string())))
					++loaded;
			}
			if (loaded > 0 && options.verbose)
				diagnostics.Report(Severity::Info, "I-FONT-ENGINE", "using Roboto from " + ToForwardSlashes(engine));
		}
	}

	if (loaded == 0)
	{
		diagnostics.Report(Severity::Error, "E-FONT-NONE",
			"no font faces registered; pass --font <file>, or --engine <dir> to use the engine's Roboto, "
			"or place fonts under " +
				diagnostics.Relative(ToForwardSlashes(fonts_dir.string())));
		return false;
	}
	return true;
}

bool App::LoadDocumentFromDisk()
{
	document = context->LoadDocument(document_path);
	if (!document)
	{
		diagnostics.Report(Severity::Error, "E-DOC-LOAD", "could not load the document");
		return false;
	}
	document->Show();
	behaviours.Attach(document);
	ApplyInputClasses();
	context->Update();
	localizer.ApplyToDocument(document, options.strict);
	context->Update();
	ApplyTextScale();
	CollectSpritesFromDiagnostics();
	return true;
}

void App::ApplyTextScale()
{
	if (!document || !context || options.text_scale == 1.0f)
		return;

	// The authored size read back before anything is written over it, exactly as the engine does it,
	// and in dp so the number means the same thing at any viewport size.
	const float dp_ratio = std::max(0.01f, context->GetDensityIndependentPixelRatio());
	const float base = document->GetComputedValues().font_size() / dp_ratio;
	if (base <= 0.0f)
		return;

	document->SetProperty("font-size", std::to_string(base * options.text_scale) + "dp");
	context->Update();
}

void App::CollectSpritesFromDiagnostics()
{
	// Two routes resolve sprite names without ever reaching the render interface, so the
	// placeholder texture never sees them and their warning is the only trace:
	//
	//   ninepatch decorator   "Could not find sprite named '<name>' in ninepatch decorator."
	//   img element           "Could not find sprite '<name>' specified in img element ..."
	//
	// The ninepatch route is the only one that also carries a file and line.
	static const std::string prefixes[] = {
		"Could not find sprite named '",
		"Could not find sprite '",
	};

	for (const Record& record : diagnostics.Records())
	{
		for (const std::string& prefix : prefixes)
		{
			const size_t begin = record.message.find(prefix);
			if (begin == std::string::npos)
				continue;

			const size_t name_begin = begin + prefix.size();
			const size_t name_end = record.message.find('\'', name_begin);
			if (name_end == std::string::npos)
				continue;

			missing_assets.RecordSprite(record.message.substr(name_begin, name_end - name_begin));
			break;
		}
	}

	RecordImageElementSizes();
}

void App::RecordImageElementSizes()
{
	// Neither sprite-only route reaches LoadTexture, so the size the placeholder would have
	// been drawn at was never measured and the entry lands in ASSETS_WANTED.md as 0x0 - which
	// is the one column an artist actually needs. An img element still has a laid-out box, so
	// recover the size from the element itself. A ninepatch has no element to ask.
	if (!document)
		return;

	Rml::ElementList images;
	document->QuerySelectorAll(images, "img");

	for (Rml::Element* image : images)
	{
		const Rml::String name = image->GetAttribute<Rml::String>("sprite", "");
		if (name.empty() || missing_assets.Sprites().count(name) == 0)
			continue;

		const Rml::Vector2f size = image->GetBox().GetSize(Rml::BoxArea::Content);
		missing_assets.RecordSize(name, int(size.x + 0.5f), int(size.y + 0.5f));
	}
}

void App::WriteAssetsWanted()
{
	if (!options.write_assets_wanted || missing_assets.Empty())
		return;

	const std::string path = (fs::path(root) / "ASSETS_WANTED.md").string();
	std::string error;
	if (!missing_assets.WriteMarkdown(path, error))
		diagnostics.Report(Severity::Warning, "W-ASSETS-WRITE", "could not update ASSETS_WANTED.md: " + error);
}

void App::RenderFrame()
{
	system_interface->AdvanceTime(options.dt_ms / 1000.0);
	context->Update();
	behaviours.UpdateScrollState();
	Backend::BeginFrame();
	context->Render();
	Backend::EndFrame();
}

void App::PumpFrames(int count)
{
	for (int i = 0; i < count; ++i)
	{
		RenderFrame();
		if (i + 1 < count)
			Backend::SwapBuffers();
	}
}

float App::ComputeDpRatio(int width, int height) const
{
	if (options.dp_ratio_fixed || options.dp_reference <= 0)
	{
		return options.dp_ratio;
	}

	// Unreal's default UI scaling rule keys off the shorter viewport dimension, so a window that
	// gets wider does not grow the HUD but one that gets taller does. Matching it means a
	// document previewed at some window size looks like the game does at that resolution.
	const int shortest = std::min(width, height);
	return float(shortest) / float(options.dp_reference);
}

void App::ApplyViewportSize(int width, int height)
{
	if (!context || width <= 0 || height <= 0)
	{
		return;
	}

	context->SetDimensions(Rml::Vector2i(width, height));
	context->SetDensityIndependentPixelRatio(ComputeDpRatio(width, height));

	// Geometry is generated in pixels here, so one rendered pixel is one unit. The backend also
	// multisamples, which makes this redundant in the preview - it is set anyway so both renderers
	// produce the same vertices and a geometry diff between them stays meaningful.
	Rml::MeshUtilities::SetBackgroundAntialiasWidth(1.0f);

	viewport_width = width;
	viewport_height = height;

	// outside-blur inverts against the viewport, and the render interface has no other way to
	// learn its size: the backend sets it through a base-class pointer and a non-virtual setter.
	if (render_interface)
	{
		render_interface->SetContextSize(Rml::Vector2i(width, height));
	}
}

int App::Run()
{
	const int result = options.IsOneShot() ? RunOneShot() : RunInteractive();

	// After the run, so the counts reflect what the document actually asked for rather than what
	// was registered.
	if (options.font_report && font_engine)
	{
		std::printf("%s\n  %s\n  msdf-draws=%d\n", Folio::GetCoreVersionString().c_str(),
			font_engine->GetReport().c_str(), render_interface->MsdfDrawCount());

		// The same scopes Insights shows in an engine build, totalled, so the numbers mean the
		// same thing in the tool where most of the iteration happens.
		const std::string trace = TraceReport::Format();
		if (!trace.empty())
		{
			std::printf("%s", trace.c_str());
		}
	}

	return result;
}

bool App::CaptureTo(const std::string& path)
{
	int width = 0, height = 0;
	Backend::GetFramebufferSize(width, height);

	std::vector<unsigned char> rgba;
	std::string error;
	if (!CaptureBackBuffer(width, height, rgba, error))
	{
		diagnostics.Report(Severity::Error, "E-CAPTURE", error);
		return false;
	}

	// After the read rather than in the renderer: the document is drawn exactly as it would be in
	// game, and what changes is only the picture someone is about to look at.
	SimulateImage(rgba, options.simulate);

	if (!WritePng(path, rgba, width, height, error))
	{
		diagnostics.Report(Severity::Error, "E-PNG-WRITE", "could not write " + path + ": " + error);
		return false;
	}

	screenshots.push_back(path);
	return true;
}

std::vector<std::string> App::OpenedFiles() const
{
	const std::set<std::string>& opened = file_interface->Opened();
	return std::vector<std::string>(opened.begin(), opened.end());
}

void App::DumpTree() const
{
	// The text companion to --keys: it shows the navigation graph without needing images.
	const std::function<void(Rml::Element*, int)> walk = [&](Rml::Element* element, int depth) {
		if (!element)
			return;

		std::string line((size_t)depth * 2, ' ');
		line += element->GetTagName();
		if (const Rml::String& id = element->GetId(); !id.empty())
			line += "#" + id;
		if (const Rml::String classes = element->GetClassNames(); !classes.empty())
			line += "." + classes;

		for (const char* attribute : {"tab-index", "autofocus", "data-loc"})
		{
			if (element->HasAttribute(attribute))
				line += std::string(" ") + attribute + "=" + element->GetAttribute<Rml::String>(attribute, "");
		}
		for (const char* property : {"nav-up", "nav-down", "nav-left", "nav-right"})
		{
			const Rml::Property* value = element->GetProperty(property);
			// Only the elements that participate in navigation are interesting here.
			if (value && value->ToString() != "none")
				line += std::string(" ") + property + ":" + value->ToString();
		}
		if (element->IsPseudoClassSet("focus"))
			line += " [focus]";

		std::printf("%s\n", line.c_str());

		for (int i = 0; i < element->GetNumChildren(); ++i)
			walk(element->GetChild(i), depth + 1);
	};

	walk(document, 0);
}

/**
 * One PNG per frame, from the first.
 *
 * The rest of the tool settles a document and then looks at it, which is the right question about a
 * layout and the wrong one about motion: everything an entry animation does has already happened by
 * the time --frames has run. So this captures from the document's first update instead, and the
 * manifest carries the step so a viewer can put the frames back on a clock.
 */
bool App::CaptureFilmstrip()
{
	const fs::path directory = options.filmstrip;

	std::error_code ec;
	fs::create_directories(directory, ec);

	nlohmann::ordered_json manifest;
	manifest["dt_ms"] = options.dt_ms;
	manifest["count"] = options.filmstrip_frames;
	manifest["frames"] = nlohmann::ordered_json::array();

	for (int i = 0; i < options.filmstrip_frames; ++i)
	{
		char name[32];
		std::snprintf(name, sizeof(name), "frame-%04d.png", i);

		RenderFrame();
		if (!CaptureTo((directory / name).string()))
			return false;

		manifest["frames"].push_back(name);

		// The last frame stays unswapped, so whatever RunOneShot captures next is that frame.
		if (i + 1 < options.filmstrip_frames)
			Backend::SwapBuffers();
	}

	// Written last, so a reader that finds it knows every frame it names is on disk.
	std::ofstream out(directory / "filmstrip.json");
	if (!out)
	{
		diagnostics.Report(Severity::Error, "E-FILMSTRIP",
			"could not write " + (directory / "filmstrip.json").string());
		return false;
	}

	out << manifest.dump(2) << "\n";
	return true;
}

bool App::CheckFlashing()
{
	// WCAG 2.3.1, as a game can honestly apply it: more than three flashes in any one second is a
	// seizure risk, and a flash is a pair of opposing changes in relative luminance of at least 10%
	// where the darker of the two is below 0.8.
	//
	// Whole-frame average rather than a 10-degree field, which is what the standard actually measures.
	// That is deliberately the conservative direction for false positives and the risky one for false
	// negatives: a blinking caret cannot trip it, and a strobe over a quarter of the screen is diluted
	// by the three quarters that are still. Judge anything near the threshold by eye as well.
	int width = 0;
	int height = 0;
	Backend::GetFramebufferSize(width, height);

	std::vector<double> luminance;
	luminance.reserve((size_t)options.filmstrip_frames);

	for (int i = 0; i < options.filmstrip_frames; ++i)
	{
		RenderFrame();

		std::vector<unsigned char> rgba;
		std::string error;
		if (!CaptureBackBuffer(width, height, rgba, error))
		{
			diagnostics.Report(Severity::Error, "E-CAPTURE", error);
			return false;
		}

		double total = 0.0;
		size_t counted = 0;

		// Every sixteenth pixel. A flash is a property of the whole frame, and reading four million of
		// them per frame would make this the slowest check by an order of magnitude.
		for (size_t p = 0; p + 3 < rgba.size(); p += 4 * 16)
		{
			const auto channel = [](unsigned char value) {
				const double v = value / 255.0;
				return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
			};

			total += 0.2126 * channel(rgba[p]) + 0.7152 * channel(rgba[p + 1])
				+ 0.0722 * channel(rgba[p + 2]);
			++counted;
		}

		luminance.push_back(counted > 0 ? total / double(counted) : 0.0);

		if (i + 1 < options.filmstrip_frames)
			Backend::SwapBuffers();
	}

	// A flash is a pair of opposing changes, so transitions are counted and halved rather than counted
	// as flashes each.
	const double frame_seconds = options.dt_ms / 1000.0;
	const int window = std::max(1, (int)std::lround(1.0 / std::max(0.0001, frame_seconds)));

	int worst = 0;
	for (size_t start = 0; start + 1 < luminance.size(); ++start)
	{
		int transitions = 0;
		for (size_t i = start; i + 1 < luminance.size() && i < start + (size_t)window; ++i)
		{
			const double low = std::min(luminance[i], luminance[i + 1]);
			const double change = std::abs(luminance[i + 1] - luminance[i]);
			if (change >= 0.1 && low < 0.8)
				++transitions;
		}

		worst = std::max(worst, transitions / 2);
	}

	if (worst > 3)
	{
		diagnostics.Report(Severity::Warning, "W-A11Y-FLASH",
			"this document flashes " + std::to_string(worst) + " times a second, over the limit of 3 - "
			"a seizure risk, and something platform certification asks about",
			options.document, 0, {{"flashesPerSecond", worst}, {"limit", 3}});
	}
	else
	{
		diagnostics.Report(Severity::Info, "I-A11Y-FLASH",
			"no flashing over the limit in " + std::to_string(options.filmstrip_frames) + " frames",
			options.document, 0, {{"flashesPerSecond", worst}, {"limit", 3}});
	}

	return true;
}

int App::RunOneShot()
{
	if (options.debug && !options.screenshot.empty())
	{
		diagnostics.Report(Severity::Warning, "W-DEBUG-SCREENSHOT",
			"--debug puts the debugger's own chrome in the captured image");
	}

	if (!options.filmstrip.empty())
	{
		if (!CaptureFilmstrip())
			return 4;
	}
	else if (options.flash_check)
	{
		if (!CheckFlashing())
			return 4;
	}
	else
	{
		PumpFrames(options.frames);
	}

	// Renders, throws every cached texture away, and renders again. What is captured must be
	// identical to what a run without this produced: a texture that does not come back, or comes
	// back wrong, is a lifetime bug in the reload path rather than in anything a document did.
	if (options.reload_textures)
	{
		Rml::ReleaseTextures();
		PumpFrames(options.frames);
	}

	if (!options.keys.empty())
	{
		std::vector<InputStep> steps;
		std::string error;
		if (!ParseInputScript(options.keys, steps, error))
		{
			diagnostics.Report(Severity::Error, "E-KEYS", error);
			return 2;
		}

		if (!options.shot_each.empty())
		{
			std::error_code ec;
			fs::create_directories(options.shot_each, ec);
		}

		focus_reports = RunInputScript(*this, steps, options.shot_each, diagnostics);

		// Program output goes to stdout; diagnostics own stderr.
		std::string sequence;
		for (const FocusReport& report : focus_reports)
		{
			std::printf("step=%d key=%s focus=%s", report.step, report.token.c_str(), report.description.c_str());
			if (!report.screenshot.empty())
				std::printf(" shot=%s", report.screenshot.c_str());
			std::printf("\n");

			if (report.step > 0)
			{
				if (!sequence.empty())
					sequence += ",";
				sequence += report.focus.empty() ? "none" : report.focus;
			}
		}
		std::printf("focus-sequence: %s\n", sequence.c_str());
	}

	// Capture sits between the renderer resolving its layers and the swap discarding them.
	if (!options.screenshot.empty() && !CaptureTo(options.screenshot))
		return 4;

	Backend::SwapBuffers();

	if (options.dump_tree)
		DumpTree();

	WriteAssetsWanted();
	return 0;
}

void App::HandleFileChanges(const std::vector<std::string>& changed)
{
	// The set of files RmlUi actually opened is the dependency list: the document, every
	// imported stylesheet, every font and texture. Deriving it this way means no @import
	// parser to keep in step with RmlUi's own resolution rules.
	const std::set<std::string>& dependencies = file_interface->Opened();

	bool rebuild_document = false;
	bool reload_styles = false;
	bool reload_data = false;
	bool reload_textures = false;

	std::string data_absolute;
	if (!data_path.empty())
	{
		std::error_code ec;
		data_absolute = ToForwardSlashes(fs::weakly_canonical(data_path, ec).string());
		std::transform(data_absolute.begin(), data_absolute.end(), data_absolute.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
	}

	for (const std::string& path : changed)
	{
		if (!data_absolute.empty() && path == data_absolute)
		{
			reload_data = true;
			continue;
		}

		const size_t dot = path.find_last_of('.');
		const std::string extension = dot == std::string::npos ? std::string() : path.substr(dot);

		/*
		 * Markup and stylesheets act whether or not the document opened them.
		 *
		 * The dependency list is what RmlUi actually opened, and it is incomplete more often than it
		 * looks: a document that failed to load opened almost nothing, a stylesheet reached through a
		 * cached template was never opened by this process, and a file that does not exist yet cannot
		 * be in it. Every one of those is a change the author expects to see, and a reload of a
		 * document that did not need one costs a frame.
		 */
		const bool markup = extension == ".rml" || extension == ".rcss";

		if (!markup && dependencies.find(path) == dependencies.end())
			continue;

		if (extension == ".rml")
			rebuild_document = true;
		else if (extension == ".rcss")
			reload_styles = true;
		else if (extension == ".json")
			reload_data = true;
		else
			reload_textures = true;
	}

	if (!rebuild_document && !reload_styles && !reload_data && !reload_textures)
		return;

	diagnostics.Reset();

	if (rebuild_document)
	{
		RebuildDocument();
	}
	else if (reload_styles)
	{
		ReloadStyleSheets(context);
		std::printf("reloaded stylesheets\n");
	}
	else if (reload_textures)
	{
		render_interface->ClearPlaceholders();
		Rml::ReleaseTextures();
		Rml::ReleaseCompiledGeometry();
		std::printf("reloaded textures\n");
	}

	if (reload_data && !rebuild_document)
	{
		data_model.Reload();
		for (const std::unique_ptr<JsonDataModel>& model : extra_models)
			model->Reload();
		if (localizer.IsPseudo())
		{
			data_model.TransformStrings([this](const std::string& text) { return localizer.Pseudoize(text); });
			for (const std::unique_ptr<JsonDataModel>& model : extra_models)
				model->TransformStrings([this](const std::string& text) { return localizer.Pseudoize(text); });
		}
		std::printf("reloaded data model\n");
	}

	diagnostics.PrintSummary(diagnostics.Relative(document_path));
}

void App::RebuildDocument()
{
	const DocumentState state = CaptureDocumentState(context, document);

	behaviours.Detach();
	document->Close();
	// Close is deferred: without this the old document is still alive during the reload.
	context->Update();

	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	render_interface->ClearPlaceholders();
	Rml::ReleaseTextures();
	Rml::ReleaseCompiledGeometry();

	file_interface->ClearOpened();
	document = context->LoadDocument(document_path);
	if (!document)
	{
		diagnostics.Report(Severity::Error, "E-DOC-LOAD", "could not reload the document");
		return;
	}

	document->Show();
	behaviours.Attach(document);
	ApplyInputClasses();
	// Layout has to exist before focus and scroll can be put back.
	context->Update();
	localizer.ApplyToDocument(document, options.strict);
	context->Update();
	RestoreDocumentState(document, state, diagnostics);
	context->Update();

	CollectSpritesFromDiagnostics();
	std::printf("reloaded document\n");
}

int App::RunInteractive()
{
	FileWatcher watcher;
	if (options.watch && !watcher.Start(root))
	{
		diagnostics.Report(Severity::Warning, "W-WATCH",
			"could not watch " + diagnostics.Relative(root) + "; hot reload is off");
	}

	while (Backend::ProcessEvents(context, [](Rml::Context*, Rml::Input::KeyIdentifier key, int, float, bool priority) {
		if (priority && key == Rml::Input::KI_F8)
		{
			Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
			return false;
		}
		return true;
	}))
	{
		if (options.watch)
		{
			const std::vector<std::string> changed = watcher.Drain();
			if (!changed.empty())
				HandleFileChanges(changed);
		}

		// The backend resizes its viewport on a framebuffer callback but deliberately leaves the
		// context alone, so the reflow and the rescale happen here.
		int current_width = 0, current_height = 0;
		Backend::GetFramebufferSize(current_width, current_height);
		if (current_width != viewport_width || current_height != viewport_height)
		{
			ApplyViewportSize(current_width, current_height);
		}

		// Interactive mode follows wall time rather than the fixed capture step.
		system_interface->SetTime(Backend::GetTime());
		context->Update();
		behaviours.UpdateScrollState();
		Backend::BeginFrame();
		context->Render();
		Backend::EndFrame();
		Backend::SwapBuffers();
	}
	WriteAssetsWanted();
	return 0;
}

} // namespace uipreview

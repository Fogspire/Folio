#pragma once

#include "DataModel.h"
#include "InputScript.h"
#include "Localization.h"
#include "FontCache.h"
#include "MissingAssets.h"
#include "Options.h"
#include "FolioCore/FolioBehaviours.h"

#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace Folio {
class FFolioFontEngine;
} // namespace Folio

namespace uipreview {

class Diagnostics;
class PreviewRenderInterface;
class PreviewSystemInterface;
class RootedFileInterface;

/** Owns the RmlUi context and everything hanging off it, for one run of the tool. */
class App {
public:
	App(const Options& options, Diagnostics& diagnostics);
	~App();

	bool Initialise();
	int Run();

	/**
	 * Renders one frame, advancing the virtual clock by the configured step.
	 * Does not swap, so a capture can read the resolved framebuffer.
	 */
	void RenderFrame();
	void PumpFrames(int count);
	/** Frames pumped after each scripted input, so focus animations settle. */
	int FramesPerStep() const { return options.frames; }

	Rml::Context* GetContext() const { return context; }
	Rml::ElementDocument* GetDocument() const { return document; }

	/** Reads the resolved framebuffer and writes it as a PNG. */
	bool CaptureTo(const std::string& path);
	const std::vector<std::string>& Screenshots() const { return screenshots; }

	const std::string& Root() const { return root; }
	const MissingAssets& Missing() const { return missing_assets; }
	const std::vector<FocusReport>& FocusReports() const { return focus_reports; }
	std::vector<std::string> OpenedFiles() const;

	/** Prints the element tree with the attributes that drive focus and navigation. */
	void DumpTree() const;

private:
	bool ResolveRoot();
	bool LoadFonts();

	/** Reads every UI/terms/*.terms, so `{tag:}` resolves in the preview as it does in game. */
	void LoadTerms();
	bool LoadDocumentFromDisk();
	/** Writes --text-scale onto the document root, where every `rem` resolves against it. */
	void ApplyTextScale();
	/** Picks up sprite failures that never reach the render interface. */
	void CollectSpritesFromDiagnostics();
	void RecordImageElementSizes();
	void WriteAssetsWanted();
	std::string DiscoverDataModelName() const;
	/**
	 * Every model the document names, primary first.
	 *
	 * A screen with a rebinding page or an embedded panel mounts a second model beside its own, and
	 * without this those bindings resolve to nothing and the document reports errors the game would
	 * never see.
	 */
	std::vector<std::string> DiscoverDataModelNames() const;
	std::string FindConventionalDataFile(const std::string& model_name) const;
	void MountDataModel();

	/** Creates the reserved 'folio' model, which reports the pinned device and confirm button. */
	void MountInputModel();
	/** Puts the folio-device-*, folio-input-* and folio-confirm-* classes on the document body. */
	void ApplyInputClasses();
	void HandleFileChanges(const std::vector<std::string>& changed);
	void RebuildDocument();

	int RunInteractive();
	int RunOneShot();
	/** Captures every frame from the document's first, leaving the last one unswapped so a
	    --screenshot after it is the frame the filmstrip ends on. */
	bool CaptureFilmstrip();
	/** Renders the same frames and measures how often the whole screen changes brightness. */
	bool CheckFlashing();

	const Options& options;
	Diagnostics& diagnostics;

	std::string root;
	/** The data file actually mounted: --data if given, else the conventional mock. */
	std::string data_path;
	/** Document path relative to the root, with a leading slash, as RmlUi sees it. */
	std::string document_path;

	std::unique_ptr<RootedFileInterface> file_interface;
	/** Outlives Rml::Shutdown, which still reads through it while releasing font resources. */
	std::unique_ptr<Folio::FFolioFontEngine> font_engine;
	std::unique_ptr<FileFontCache> font_cache;
	std::unique_ptr<class PreviewCoreHost> core_host;
	/** Owned by the backend, which constructs them once the GL context exists. */
	PreviewSystemInterface* system_interface = nullptr;
	PreviewRenderInterface* render_interface = nullptr;

	MissingAssets missing_assets;
	JsonDataModel data_model {diagnostics};
	/** The models beyond the first, in the order the document names them. */
	std::vector<std::unique_ptr<JsonDataModel>> extra_models;

	// Bound into the reserved model by address, so they outlive it.
	Rml::String input_device_value;
	Rml::String input_confirm_value;
	Rml::String input_back_value;
	bool input_is_gamepad = false;
	Localizer localizer {diagnostics};

	/** Ratio Unreal would use for a viewport this size, unless --dp-ratio pinned one. */
	float ComputeDpRatio(int width, int height) const;
	/** Resizes the context and rescales it to match, which is what makes a resized window behave
	    like a resized game window. */
	void ApplyViewportSize(int width, int height);

	int viewport_width = 0;
	int viewport_height = 0;

	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;

	// Dragging, dropping and closing: the same listener the Unreal host installs.
	FFolioBehaviours behaviours;
	std::vector<std::string> screenshots;
	std::vector<FocusReport> focus_reports;

	bool rml_initialised = false;
	bool backend_initialised = false;
};

} // namespace uipreview

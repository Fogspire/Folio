#pragma once

#include "DataModel.h"
#include "InputScript.h"
#include "Localization.h"
#include "MissingAssets.h"
#include "Options.h"

#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

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
	bool LoadDocumentFromDisk();
	/** Picks up sprite failures that never reach the render interface. */
	void CollectSpritesFromDiagnostics();
	void RecordImageElementSizes();
	void WriteAssetsWanted();
	std::string DiscoverDataModelName() const;
	void MountDataModel();
	void HandleFileChanges(const std::vector<std::string>& changed);
	void RebuildDocument();

	int RunInteractive();
	int RunOneShot();

	const Options& options;
	Diagnostics& diagnostics;

	std::string root;
	/** Document path relative to the root, with a leading slash, as RmlUi sees it. */
	std::string document_path;

	std::unique_ptr<RootedFileInterface> file_interface;
	/** Owned by the backend, which constructs them once the GL context exists. */
	PreviewSystemInterface* system_interface = nullptr;
	PreviewRenderInterface* render_interface = nullptr;

	MissingAssets missing_assets;
	JsonDataModel data_model {diagnostics};
	Localizer localizer {diagnostics};

	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	std::vector<std::string> screenshots;
	std::vector<FocusReport> focus_reports;

	bool rml_initialised = false;
	bool backend_initialised = false;
};

} // namespace uipreview

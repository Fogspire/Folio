#pragma once

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

	Rml::Context* GetContext() const { return context; }
	Rml::ElementDocument* GetDocument() const { return document; }

	/** Reads the resolved framebuffer and writes it as a PNG. */
	bool CaptureTo(const std::string& path);
	const std::vector<std::string>& Screenshots() const { return screenshots; }

private:
	bool ResolveRoot();
	bool LoadFonts();
	bool LoadDocumentFromDisk();
	/** Picks up sprite failures that never reach the render interface. */
	void CollectSpritesFromDiagnostics();
	void WriteAssetsWanted();

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

	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	std::vector<std::string> screenshots;

	bool rml_initialised = false;
	bool backend_initialised = false;
};

} // namespace uipreview

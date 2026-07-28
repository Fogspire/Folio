#pragma once

#include "Options.h"

#include <memory>
#include <string>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace uipreview {

class Diagnostics;
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

private:
	bool ResolveRoot();
	bool LoadFonts();
	bool LoadDocumentFromDisk();

	int RunInteractive();
	int RunOneShot();

	const Options& options;
	Diagnostics& diagnostics;

	std::string root;
	/** Document path relative to the root, with a leading slash, as RmlUi sees it. */
	std::string document_path;

	std::unique_ptr<RootedFileInterface> file_interface;
	/** Owned by the backend, which constructs it once the GL context exists. */
	PreviewSystemInterface* system_interface = nullptr;

	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;

	bool rml_initialised = false;
	bool backend_initialised = false;
};

} // namespace uipreview

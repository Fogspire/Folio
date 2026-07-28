#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace uipreview {

struct Options {
	/** Document to load, as given on the command line. */
	std::string document;
	/** Directory every path is resolved against. Empty means derive it from the document. */
	std::string root;

	/**
	 * Font faces to register. Folio bundles no typeface: the choice belongs to the game, so
	 * fonts are configuration. When empty, any font found under <root>/ui/fonts is used.
	 */
	std::vector<std::string> fonts;
	/** Unreal engine directory, used to find Roboto when no font is given. */
	std::string engine;

	int width = 1920;
	int height = 1080;
	float dp_ratio = 1.0f;

	std::string data_path;
	std::string data_model;
	std::string culture;

	std::string keys;
	std::string shot_each;
	std::string screenshot;
	std::string json_report;

	/** Frames pumped before a capture and between injected keys, so layout and entry
	    animations settle. */
	int frames = 30;
	/** Simulated milliseconds per pumped frame. Zero freezes time. */
	double dt_ms = 16.67;

	bool debug = false;
	bool strict = false;
	bool watch = true;
	bool verbose = false;
	bool write_assets_wanted = true;
	bool dump_tree = false;

	/** True when the run renders a fixed number of frames and exits. */
	bool IsOneShot() const { return !screenshot.empty() || !keys.empty() || dump_tree; }
};

enum class ParseResult { Ok, HelpRequested, Error };

ParseResult ParseCommandLine(int argc, char** argv, Options& out_options, std::string& out_error);

void PrintUsage(std::FILE* stream);

} // namespace uipreview

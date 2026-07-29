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
	/** True once --dp-ratio is given, which pins the ratio instead of deriving it from the
	    viewport. */
	bool dp_ratio_fixed = false;
	/** Shortest viewport side that means a ratio of 1. Unreal scales its UI the same way, off
	    the shorter dimension against a 1080 reference, so a window here behaves like a resized
	    game window rather than a fixed-size canvas. */
	int dp_reference = 1080;

	std::string data_path;
	std::string data_model;
	std::string culture;

	std::string keys;
	std::string shot_each;
	std::string screenshot;
	std::string json_report;
	/** Where every text element laid out. Written after the frames settle, so it describes the
	    same state a screenshot taken with the same arguments would. */
	std::string metrics_report;

	/** Field resolution per em, and the field's width in texels. A field resolves best near 1:1,
	    so this trades small-text clarity against magnification headroom. */
	int msdf_px_em = 48;
	int msdf_px_range = 12;
	/** Multiplies the antialiasing spread. Above 1 narrows the edge ramp, which reads crisper at
	    small sizes at the cost of some aliasing. */
	float msdf_sharpen = 1.0f;

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
	/** Report the text engine. Alone it prints what the binary was built against; with a
	    document it also reports the faces and the atlases they cost. */
	bool font_report = false;
	/** Release every cached texture between two renders, then capture. Proves a hot reload does
	    not lose a texture it still needs, which is otherwise only visible by watching. */
	bool reload_textures = false;
	/** Read and write generated fields under <root>/.folio-cache. */
	bool font_cache = true;

	/** True when the run renders a fixed number of frames and exits. */
	bool IsOneShot() const { return !screenshot.empty() || !keys.empty() || dump_tree || !metrics_report.empty(); }
};

enum class ParseResult { Ok, HelpRequested, FontReportRequested, Error };

ParseResult ParseCommandLine(int argc, char** argv, Options& out_options, std::string& out_error);

void PrintUsage(std::FILE* stream);

} // namespace uipreview

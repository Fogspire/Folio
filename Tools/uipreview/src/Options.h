#pragma once

#include "FolioCore/FolioInputState.h"

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
	 * fonts are configuration. When empty, any font found under <root>/UI/fonts is used.
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

	// Themes to activate, in the order given. A theme is an @media (theme: name) block; several can be
	// on at once, so this repeats rather than replacing.
	std::vector<std::string> themes;

	/** What the document is told the player is holding. In game this follows real input; here it
	    is pinned, so a screenshot of the gamepad prompts is reproducible. */
	Folio::EInputDevice input_device = Folio::EInputDevice::Keyboard;
	/** Which gamepad face button confirms. A game exposes this to its settings and may default it
	    per platform, so both settings need to be viewable without a build. */
	Folio::EConfirmButton confirm_button = Folio::EConfirmButton::South;

	std::string keys;
	std::string shot_each;
	std::string screenshot;
	std::string json_report;
	/** Where every text element laid out. Written after the frames settle, so it describes the
	    same state a screenshot taken with the same arguments would. */
	std::string metrics_report;

	// Which rule set what, and where that rule lives. The seam an editing tool needs.
	std::string provenance_report;

	/** Contrast, text size, hit target, accessible name, visible focus. Implied by --a11y-report. */
	bool a11y = false;
	std::string a11y_report;
	/** WCAG AA for normal text. Large text is held to 3:1 regardless, as WCAG defines it. */
	float a11y_contrast = 4.5f;
	/** Floors, in dp. The target size is WCAG 2.2 AA; the text size is a house rule, since WCAG
	    has none - it exists because a size nobody can read is the failure people actually ship. */
	float a11y_min_text = 12.0f;
	float a11y_min_target = 24.0f;

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
	/** Shape with HarfBuzz. Off measures one glyph per codepoint with legacy kerning, which is
	    what a build without HarfBuzz does and what a metrics diff is taken against. */
	bool complex_shaping = true;

	/** True when the run renders a fixed number of frames and exits. */
	bool IsOneShot() const { return !screenshot.empty() || !keys.empty() || dump_tree || !metrics_report.empty()
		|| !provenance_report.empty() || !a11y_report.empty() || a11y; }
};

enum class ParseResult { Ok, HelpRequested, FontReportRequested, Error };

ParseResult ParseCommandLine(int argc, char** argv, Options& out_options, std::string& out_error);

void PrintUsage(std::FILE* stream);

} // namespace uipreview

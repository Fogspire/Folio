#pragma once

#include <cstdio>
#include <string>

namespace uipreview {

class App;
class Diagnostics;
struct Options;

/**
 * The checks that do not need a screen reader.
 *
 * Contrast, text size, hit target size, an accessible name, and a visible focus state. None of them
 * need platform accessibility plumbing, and all of them are things that get expensive to fix later:
 * they are spread across every document rather than concentrated in one place, so the cost of adding
 * them at the end scales with how much UI exists by then.
 *
 * Everything is reported through Diagnostics, so --strict gates a build on it and the JSON report
 * carries it, rather than this being a separate thing to remember to run.
 */
void CheckAccessibility(const Options& options, App& app, Diagnostics& diagnostics);

/** The findings again, as JSON, for a caller that wants to track them over time. */
bool WriteAccessibilityReport(const std::string& path, const Options& options, App& app,
	Diagnostics& diagnostics, std::string& out_error);

/**
 * The tree a screen reader is read out of, as JSON.
 *
 * The same walk the engine builds its Slate accessibility tree from, so what this prints is what a
 * player using NVDA hears, in the order they hear it - checkable in a diff rather than by listening.
 */
bool WriteAccessibilityTree(const std::string& path, const Options& options, App& app,
	std::string& out_error);

/** The same tree, indented, for reading rather than diffing. */
void PrintAccessibilityTree(App& app, std::FILE* stream);

} // namespace uipreview

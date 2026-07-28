#pragma once

#include <string>

namespace uipreview {

class App;
class Diagnostics;
struct Options;

/**
 * Writes the run as structured data.
 *
 * This is the interface for a tool, a CI step or a model to answer "is this document valid,
 * and does its navigation work" without scraping stderr, and its `documents` array doubles
 * as the dependency manifest for the document.
 */
bool WriteJsonReport(const std::string& path, const Options& options, const App& app, const Diagnostics& diagnostics,
	int exit_code, std::string& out_error);

} // namespace uipreview

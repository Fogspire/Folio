#pragma once

#include <string>

namespace uipreview {

class App;
class Diagnostics;
struct Options;

/**
 * Writes where every piece of text ended up.
 *
 * This exists for one job: diffing a document's layout across a change to the text engine.
 * Advances shift when the engine does - Folio's are unhinted and scaled linearly from the design
 * units, which is what makes one atlas serve every size - and a shifted advance moves a line
 * break, which moves everything below it. The change is usually right; it still has to be seen
 * rather than discovered in a screenshot weeks later.
 *
 * So the report records the wrapped lines themselves, not just box sizes. A box can stay the
 * same size while the words inside it break somewhere new.
 */
bool WriteMetricsReport(const std::string& path, const Options& options, const App& app,
	const Diagnostics& diagnostics, std::string& out_error);

} // namespace uipreview

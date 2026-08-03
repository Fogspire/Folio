#pragma once

namespace uipreview {

class App;
class Diagnostics;
struct Options;

/**
 * The costs a document cannot show you, and a screenshot cannot either.
 *
 * One check so far, and it is the one that has actually cost frames: a layer effect on an element
 * the size of the screen. `box-shadow`, `filter`, `backdrop-filter` and `mask-image` are each drawn
 * by rendering the element into an offscreen target and reading it back, so on a full-screen
 * element that is a full-screen target allocated, cleared, drawn and composited - and on a document
 * with a running animation, every frame of it.
 *
 * A warning rather than a note, because it is invisible in the preview and in the engine both: the
 * effect looks exactly as intended and only a profiler says what it cost. `--strict` is what stops
 * it shipping.
 */
void CheckPerformance(const Options& options, App& app, Diagnostics& diagnostics);

} // namespace uipreview

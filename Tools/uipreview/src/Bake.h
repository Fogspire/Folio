#pragma once

#include <string>

namespace uipreview {

class App;
class Diagnostics;
struct Options;

/**
 * Renders one element to a PNG with alpha, so a document can become the art it draws.
 *
 * The case it exists for is an effect that is cheap to author and expensive to run: a full-screen
 * vignette out of an inset `box-shadow`, a soft glow behind a panel, a masked plate. Each of those
 * is a render-target pass every frame, and none of it changes - so it should be a texture, and the
 * document that describes it should be the thing that produces the texture.
 *
 * Nothing here is specific to any one effect. Any element in any document can be baked, named by
 * the same selector syntax RCSS uses.
 *
 *     uipreview UI/screens/edges_baker.rml --bake "#burning=UI/art/edge_burning.png"
 *
 * The element is rendered alone, on nothing, cropped to its border box, and written straight-alpha
 * so the engine's own premultiply is not applied twice.
 */
bool RunBakes(const Options& options, App& app, Diagnostics& diagnostics);

/**
 * Splits a `selector=path` argument.
 *
 * The last `=` separates them, because a selector may contain one - `[data-state=on]` is ordinary -
 * and a Windows path never does.
 */
bool ParseBakeArgument(const std::string& argument, std::string& out_selector, std::string& out_path);

} // namespace uipreview

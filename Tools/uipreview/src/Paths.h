#pragma once

#include <string>

namespace uipreview {

/** Directory containing the running executable, forward slashed and without a trailing slash. */
std::string ExecutableDirectory();

/**
 * The Folio plugin directory, so the tool can find what Folio itself ships regardless of
 * which content root it was pointed at.
 */
std::string FolioRoot();

/**
 * The content root's UI tree, holding fonts, mock data, localisation and the documents.
 *
 * `UI` to match Content and every other directory Unreal puts beside it, but a lower-case one is
 * taken if that is what is there. Windows cannot tell them apart and Linux can, so a project
 * written on one and previewed on the other would otherwise lose its fonts.
 */
std::string UiDirectory(const std::string& root);

/**
 * Locates an Unreal engine directory, so the tool can default to the same Roboto the plugin
 * uses in engine. Checks --engine, then UE_ENGINE_DIR, then walks up from the executable and
 * the working directory looking for Engine/Content/Slate/Fonts.
 */
std::string FindEngineDirectory(const std::string& explicit_directory);

} // namespace uipreview

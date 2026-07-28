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
 * Locates an Unreal engine directory, so the tool can default to the same Roboto the plugin
 * uses in engine. Checks --engine, then UE_ENGINE_DIR, then walks up from the executable and
 * the working directory looking for Engine/Content/Slate/Fonts.
 */
std::string FindEngineDirectory(const std::string& explicit_directory);

} // namespace uipreview

#pragma once

#include <RmlUi/Core/Input.h>

#include <string>
#include <vector>

namespace uipreview {

class App;
class Diagnostics;

struct InputStep {
	enum class Kind { Key, Wait, Click, Text };

	Kind kind = Kind::Key;
	std::string token;
	Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
	int modifiers = 0;
	/** Selector for Click, literal text for Text, frame count for Wait. */
	std::string argument;
	int frames = 0;
};

/** Parses the --keys list. Returns false and fills out_error on an unknown token. */
bool ParseInputScript(const std::string& text, std::vector<InputStep>& out_steps, std::string& out_error);

struct FocusReport {
	int step = 0;
	std::string token;
	/** Selector identifying the focused element, or empty when nothing has focus. */
	std::string focus;
	std::string description;
	std::string screenshot;
};

/**
 * Runs the script, reporting the focused element after each input.
 *
 * This is the part a static screenshot cannot verify: whether focus actually moves where the
 * markup says it should. The reported sequence is deliberately assertable as plain text.
 */
std::vector<FocusReport> RunInputScript(App& app, const std::vector<InputStep>& steps, const std::string& shot_directory,
	Diagnostics& diagnostics);

} // namespace uipreview

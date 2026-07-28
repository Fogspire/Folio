#pragma once

#include "RmlUi_Platform_GLFW.h"

namespace uipreview {

class Diagnostics;

/**
 * Adds log capture and a caller-driven clock to the GLFW platform layer, which already
 * supplies cursors and clipboard.
 *
 * The clock is virtual rather than wall time so that a screenshot is reproducible frame for
 * frame regardless of machine speed: capture modes advance it by a fixed step per rendered
 * frame, and a step of zero freezes animation entirely.
 */
class PreviewSystemInterface : public SystemInterface_GLFW {
public:
	explicit PreviewSystemInterface(Diagnostics& diagnostics) : diagnostics(diagnostics) {}

	double GetElapsedTime() override { return time; }
	void SetTime(double seconds) { time = seconds; }
	void AdvanceTime(double seconds) { time += seconds; }

	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

	/**
	 * Identity. Localization is applied as a pass over data-loc attributes after load
	 * instead, because this hook sees text with no element context: it cannot tell whether
	 * the owning element carries a key, so it can neither look one up nor flag untagged text.
	 */
	int TranslateString(Rml::String& translated, const Rml::String& input) override;

private:
	Diagnostics& diagnostics;
	double time = 0.0;
};

} // namespace uipreview

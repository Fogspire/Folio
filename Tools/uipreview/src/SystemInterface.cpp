#include "SystemInterface.h"

#include "Diagnostics.h"

namespace uipreview {

bool PreviewSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
	diagnostics.FromRmlUi(type, message);

	// Returning false interrupts RmlUi's execution, which is only wanted for a failed assert.
	return type != Rml::Log::LT_ASSERT;
}

int PreviewSystemInterface::TranslateString(Rml::String& translated, const Rml::String& input)
{
	translated = input;
	return 0;
}

} // namespace uipreview

#include "FolioSystemInterface.h"

#include "FolioModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Internationalization/Text.h"

double FFolioSystemInterface::GetElapsedTime()
{
	return FPlatformTime::Seconds() - GStartTime;
}

bool FFolioSystemInterface::LogMessage(const Rml::Log::Type Type, const Rml::String& Message)
{
	const FString Text = Folio::ToFString(Message);

	switch (Type)
	{
	case Rml::Log::LT_ALWAYS:
	case Rml::Log::LT_ERROR:
		UE_LOG(LogFolio, Error, TEXT("%s"), *Text);
		break;

	case Rml::Log::LT_ASSERT:
		UE_LOG(LogFolio, Error, TEXT("assert: %s"), *Text);
		// The one case where interrupting is correct: RmlUi's own invariant has been broken
		// and continuing runs undefined behaviour.
		return false;

	case Rml::Log::LT_WARNING:
		// Nearly every RCSS problem arrives here rather than as an error: unknown properties,
		// bad decorators, unresolved sprites. Warning level is not optional for this one.
		UE_LOG(LogFolio, Warning, TEXT("%s"), *Text);
		break;

	case Rml::Log::LT_INFO:
		UE_LOG(LogFolio, Log, TEXT("%s"), *Text);
		break;

	default:
		UE_LOG(LogFolio, Verbose, TEXT("%s"), *Text);
		break;
	}

	return true;
}

void FFolioSystemInterface::SetClipboardText(const Rml::String& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*Folio::ToFString(Text));
}

void FFolioSystemInterface::GetClipboardText(Rml::String& OutText)
{
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	OutText = Folio::ToRmlString(Text);
}

void FFolioSystemInterface::SetMouseCursor(const Rml::String& CursorName)
{
	// RCSS cursor values are application defined strings rather than CSS keywords, so the
	// mapping is ours to choose. The widget applies it; the name is recorded here for it.
	CurrentCursor = CursorName;
}

int FFolioSystemInterface::TranslateString(Rml::String& OutTranslated, const Rml::String& Input)
{
	// Documents carry their source text inline and their key in data-loc, so by the time a
	// string reaches here it has already been through the document's own pass. Nothing to do
	// unless a key survived, which means the gather step has not run for this document.
	OutTranslated = Input;
	return 0;
}

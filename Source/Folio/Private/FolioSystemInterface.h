#pragma once

#include "FolioRmlUi.h"

/**
 * Elapsed time, logging, clipboard and translation.
 *
 * RmlUi calls this for anything it cannot do itself. It is set once for the process.
 */
class FFolioSystemInterface final : public Rml::SystemInterface
{
public:
	virtual double GetElapsedTime() override;

	/**
	 * Returning false interrupts RmlUi's execution, which is only ever wanted for an assert.
	 * Everything else logs and continues, including LT_ERROR: a malformed document should
	 * leave the rest of the UI running.
	 */
	virtual bool LogMessage(Rml::Log::Type Type, const Rml::String& Message) override;

	virtual void SetClipboardText(const Rml::String& Text) override;
	virtual void GetClipboardText(Rml::String& OutText) override;

	virtual void SetMouseCursor(const Rml::String& CursorName) override;

	/**
	 * Resolves a data-loc key through FText, so RML text reaches the same localisation
	 * pipeline as everything else rather than a parallel one. Returns the number of
	 * substitutions made; zero leaves the source text in place.
	 */
	virtual int TranslateString(Rml::String& OutTranslated, const Rml::String& Input) override;

	/** The cursor name RmlUi last asked for. The widget owns turning it into a real cursor. */
	const Rml::String& GetCurrentCursor() const { return CurrentCursor; }

private:
	Rml::String CurrentCursor;
};

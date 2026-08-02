#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioAccessibilityLibrary.generated.h"

/**
 * What a game drives, on top of markup that is already annotated.
 *
 * A document annotated the way docs/accessibility.md describes is walked into a Slate accessibility
 * tree whenever a screen reader is listening, with no game code at all. What a game still owns is the
 * setting that turns its own reader on, and the things no element can say by existing.
 */
UCLASS(MinimalAPI)
class UFolioAccessibilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Turns the game's own screen reader on or off, and speaks the focused control from then on.
	 *
	 * This is the setting a player finds in an options menu. It is separate from a player running
	 * NVDA or a console narrator, who needs nothing turned on: that path reads the same tree through
	 * the platform. Off by default, always - a menu that starts talking unasked is a bug.
	 *
	 * Save it with the rest of your settings and apply it on load, before the first menu opens.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetScreenReader(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsScreenReaderEnabled();

	/**
	 * How fast it talks: -10 to 10, zero being the voice's own speed.
	 *
	 * Worth putting beside the toggle. Someone who relies on a screen reader usually runs it far
	 * faster than it sounds reasonable to anyone who does not.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetSpeechRate(int32 Rate);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API int32 GetSpeechRate();

	/** 0 to 100, against the system voice's own volume. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void SetSpeechVolume(int32 Volume);

	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API int32 GetSpeechVolume();

	/** Whether this platform can speak at all. False hides the setting rather than offering a dead one. */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsSpeechAvailable();
	/**
	 * Says something out loud, wherever the reader happens to be.
	 *
	 * For what no element can carry: a save completing, a round starting, a warning that is gone
	 * before anyone could navigate to it. A screen reader only reads what it is standing on.
	 *
	 * Best effort by nature - two announcements close together can stomp one another on some
	 * platforms - so never make this the only way a player can learn something.
	 *
	 * Markup asks for the same thing with `folio-announce` on a line that rewrites itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Accessibility")
	static FOLIO_API void Announce(const FString& Text);

	/**
	 * Whether anything is listening, by either route.
	 *
	 * True once the platform reports a screen reader has asked for the application's accessibility
	 * tree, and true while the game's own reader is on. Worth reading for the things that are only
	 * worth doing for that player - an audio cue on a state a sighted player can see, a slower default
	 * timer - and not for hiding UI from them.
	 */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsScreenReaderActive();
};

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioAccessibilityLibrary.generated.h"

/**
 * The two things a game needs from the screen reader that a document cannot say by existing.
 *
 * Everything else is markup: a document annotated the way docs/accessibility.md describes is walked
 * into a Slate accessibility tree whenever something is listening, with no game code at all.
 */
UCLASS(MinimalAPI)
class UFolioAccessibilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
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
	 * Whether anything is listening.
	 *
	 * True once the platform reports a screen reader has asked for the application's accessibility
	 * tree. Worth reading for the things that are only worth doing for that player - an audio cue on
	 * a state a sighted player can see, a slower default timer - and not for hiding UI from them.
	 */
	UFUNCTION(BlueprintPure, Category = "Folio|Accessibility")
	static FOLIO_API bool IsScreenReaderActive();
};

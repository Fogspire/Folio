#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioTheme.generated.h"

/**
 * Themes, which are how a document has two looks without having two stylesheets.
 *
 * RCSS has no custom properties and no `var()`, so a theme cannot be a swapped value. It is a media
 * query: a stylesheet writes `@media (theme: high-contrast) { ... }` and those rules apply only while
 * that theme is on. Which means a theme is an *override block*, and the rules it overrides have to be
 * overridable - the reason nothing in Folio's component library contains a colour.
 *
 * A theme is global here, not per document. RmlUi tracks it per context, so turning one on walks every
 * context that exists and is remembered for every context made later; otherwise a screen opened after
 * the switch would come up in the old look.
 *
 * Several can be on at once. They are independent switches rather than a list to choose from, so
 * `colour-blind` and `large-text` compose without a stylesheet needing to know about both.
 */
UCLASS(MinimalAPI)
class UFolioThemeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Turns a theme on or off everywhere, now and for documents not yet loaded.
	 *
	 * Every document redraws: a media query changing is not something RmlUi reports as a change, so a
	 * settled document would otherwise keep the look it already had.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Theme")
	static FOLIO_API void SetTheme(FName Theme, bool bActive);

	UFUNCTION(BlueprintPure, Category = "Folio|Theme")
	static FOLIO_API bool IsThemeActive(FName Theme);

	/** Every theme currently on, for a settings screen showing what is in force. */
	UFUNCTION(BlueprintPure, Category = "Folio|Theme")
	static FOLIO_API TArray<FName> GetActiveThemes();
};

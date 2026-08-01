#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioTextureLibrary.generated.h"

class UTexture;

/**
 * The named texture registry, for Blueprint and for game code with no widget to hand.
 *
 * Prefer `UFolioWidget::RegisterTexture` where there is a widget: it releases its names when the
 * screen goes away, and these do not. See Folio::RegisterTexture for why names are global.
 */
UCLASS()
class FOLIO_API UFolioTextureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Makes a texture reachable from markup as `folio://<name>`. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	static void RegisterFolioTexture(FName Name, UTexture* Texture);

	/** Drops a name. Documents drawing it fall back to nothing until it is registered again. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	static void UnregisterFolioTexture(FName Name);

	/** The texture behind a name, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Folio")
	static UTexture* FindFolioTexture(FName Name);
};

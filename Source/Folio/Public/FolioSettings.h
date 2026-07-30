#pragma once

#include "FolioWorldDocument.h"

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "FolioSettings.generated.h"

/**
 * Project-wide Folio defaults, under Project Settings > Plugins > Folio.
 *
 * Only for choices a game makes once and then wants applied everywhere. Anything a single document
 * might reasonably disagree about stays a property on the document.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Folio"))
class FOLIO_API UFolioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/**
	 * What a newly created world-space document draws as, before anyone sets its own mode.
	 *
	 * After tonemap unless a project deliberately chooses otherwise: avoiding the temporal resolve is the
	 * reason the world path exists, so a document has to opt in to being smeared rather than out of it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "World-space documents")
	EFolioWorldDrawMode DefaultWorldDrawMode = EFolioWorldDrawMode::AfterTonemap;
};

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

	/**
	 * Directories the document picker does not offer, relative to the project.
	 *
	 * A game accumulates markup that is not a screen anyone opens - fragments, templates, a scratch
	 * directory - and a picker listing all of it is as unhelpful as no picker. Prefixes rather than
	 * exact paths, so one entry hides a tree.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Authoring")
	TArray<FString> HiddenDocumentDirectories;

	/**
	 * Whether the picker offers the documents Folio itself ships.
	 *
	 * On, because they are what a new project has to look at. Off once a game has its own screens and
	 * the examples are only in the way.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Authoring")
	bool bShowPluginDocumentsInPicker = true;
};

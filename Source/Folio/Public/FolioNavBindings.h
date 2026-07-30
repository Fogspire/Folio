#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioNavBindings.generated.h"

/**
 * What a key means to a document, independent of any particular key.
 *
 * The built-in schemes - arrows, WASD, IJKL, the gamepad - are expressed in these terms too, so a
 * rebinding screen and Folio's own defaults are describing the same thing rather than two systems
 * that have to agree.
 */
UENUM(BlueprintType)
enum class EFolioNavAction : uint8
{
	None,
	Up,
	Down,
	Left,
	Right,
	/** Activates the focused element. RmlUi turns this into a click. */
	Confirm,
	/** What a document listens for to close itself. */
	Back,
	NextTab,
	PreviousTab,
};

/** One row of a navigation binding table. */
USTRUCT(BlueprintType)
struct FFolioNavBinding : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FKey Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	EFolioNavAction Action = EFolioNavAction::None;
};

namespace Folio
{
	/** The action bound to a key, or None. Explicit bindings win over the built-in schemes. */
	FOLIO_API EFolioNavAction FindBoundAction(const FKey& Key);

	/** True when a loaded table asked to be the whole mapping, so the defaults are skipped. */
	FOLIO_API bool DefaultsReplaced();
}

/**
 * Runtime navigation rebinding.
 *
 * Nothing has to be bound for a document to navigate: the built-in schemes cover keyboard and pad
 * out of the box, and this exists for the game that wants its own. A binding set here takes
 * precedence over the defaults for that key, and is not subject to the folio.Nav.* scheme switches -
 * a project that bound a key meant it.
 */
UCLASS(MinimalAPI)
class UFolioNavLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Loads every row of a table as a binding.
	 *
	 * With bReplaceDefaults the table becomes the entire mapping and no built-in scheme applies,
	 * which is what a game with its own control scheme wants. Without it the rows are additions,
	 * so arrows and the pad keep working alongside them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio|Navigation")
	static FOLIO_API void LoadNavBindings(const UDataTable* Table, bool bReplaceDefaults = false);

	/** Binds one key, replacing any binding it had. This is what a rebinding screen calls. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Navigation")
	static FOLIO_API void SetNavBinding(FKey Key, EFolioNavAction Action);

	/** Unbinds a key. It falls back to whatever the built-in schemes say, if they are in play. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Navigation")
	static FOLIO_API void ClearNavBinding(FKey Key);

	/** Forgets every binding, so the built-in schemes alone apply again. */
	UFUNCTION(BlueprintCallable, Category = "Folio|Navigation")
	static FOLIO_API void ResetNavBindings();

	/** What a key currently means, built-in schemes included. For showing a binding to a player. */
	UFUNCTION(BlueprintPure, Category = "Folio|Navigation")
	static FOLIO_API EFolioNavAction GetNavAction(FKey Key);

	/** Every key that currently means an action, so a settings screen can list them. */
	UFUNCTION(BlueprintPure, Category = "Folio|Navigation")
	static FOLIO_API TArray<FKey> GetKeysForNavAction(EFolioNavAction Action);
};

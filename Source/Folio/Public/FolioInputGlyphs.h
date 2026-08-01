#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"

#include "FolioInputGlyphs.generated.h"

class ULocalPlayer;

/**
 * How one key is drawn in a prompt.
 *
 * A row per key, in a DataTable the project owns. Folio ships no button art: what a face button
 * looks like is a platform and licensing question, and a plugin guessing at it is how a game ends up
 * showing an Xbox glyph on a PlayStation.
 *
 * Neither field is required. With no sprite the prompt falls back to a keycap with `Label` in it,
 * which is legible everywhere and is what a keyboard prompt usually wants anyway; with no label the
 * key's own display name is used. A project with no table at all still gets working prompts.
 */
USTRUCT(BlueprintType)
struct FFolioKeyGlyph : public FTableRowBase
{
	GENERATED_BODY()

	/** The key this row describes. The row name is ignored, so a table can be keyed however suits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FKey Key;

	/** A sprite from a loaded `@spritesheet`, drawn instead of a keycap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FName Sprite;

	/** What the keycap says. Short: `SPACE`, `LMB`, `F`. Falls back to the key's display name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FText Label;
};

/** Whose names the pad's buttons go by. */
UENUM(BlueprintType)
enum class EFolioPadScheme : uint8
{
	/** A, B, X, Y and LB/RB/LT/RT. */
	Xbox,

	/** X, O, square and triangle, and L1/R1/L2/R2. */
	PlayStation,
};

/** Where a prompt's picture of a key comes from. */
UENUM(BlueprintType)
enum class EFolioGlyphSource : uint8
{
	/** The stylesheet's own shapes: a keycap, a mouse, a face button. Needs no art and no licence. */
	Drawn,

	/** A sprite from the game's spritesheet, named by the glyph table. */
	Sprite,

	/** A texture, which is how CommonInput's controller art arrives. */
	Image,
};

/** What a prompt should draw for one key. Exactly one of the three is filled in. */
USTRUCT(BlueprintType)
struct FFolioGlyph
{
	GENERATED_BODY()

	/** `drawn`, `sprite` or `image`, so markup can pick a branch without three emptiness tests. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Kind;

	/** What a drawn keycap says. Always filled in, so there is something to fall back to. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Cap;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Sprite;

	/** A `folio://` source, ready for an img. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Image;
};

namespace Folio
{
	/**
	 * Which sources to try, in order, for each kind of key.
	 *
	 * Two lists because the answer is usually different: a keyboard prompt drawn as a keycap looks
	 * better than any screenshot of a keyboard, while a PlayStation face button has art that a
	 * project has licensed and should use. Defaults to drawn everywhere, which is what makes prompts
	 * work in a project with no art and no CommonInput data at all.
	 */
	FOLIO_API void SetGlyphSources(const TArray<EFolioGlyphSource>& Keyboard,
		const TArray<EFolioGlyphSource>& Gamepad);

	/**
	 * Which console's names the pad's buttons take.
	 *
	 * Unreal's own display name for a face button is "Gamepad Face Button Bottom", which is true and
	 * useless on a keycap. Xbox by default because it is what a PC player is most likely holding; a
	 * port sets it, or a game offers it as a setting.
	 */
	FOLIO_API void SetPadScheme(EFolioPadScheme Scheme);

	FOLIO_API EFolioPadScheme GetPadScheme();

	/** Resolves one key against that policy. */
	FOLIO_API FFolioGlyph ResolveGlyph(const FKey& Key, const ULocalPlayer* LocalPlayer);

	/**
	 * Registers the project's glyph table. Repeatable: later rows win, so a platform can add a table
	 * that overrides the gamepad rows without restating the keyboard ones.
	 */
	FOLIO_API void RegisterGlyphTable(const UDataTable* Table);

	FOLIO_API void ClearGlyphTables();

	/** The sprite for a key, or NAME_None when the prompt should draw a keycap instead. */
	FOLIO_API FName GlyphFor(const FKey& Key);

	/** What a keycap should say. The table's label, else the key's own display name. */
	FOLIO_API FText LabelFor(const FKey& Key);
}

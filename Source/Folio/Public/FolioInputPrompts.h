#pragma once

#include "CoreMinimal.h"
#include "FolioInputGlyphs.h"
#include "UObject/Object.h"

#include "FolioInputPrompts.generated.h"

class ULocalPlayer;

/** One "press this to do that" line. */
USTRUCT(BlueprintType)
struct FFolioPrompt
{
	GENERATED_BODY()

	/** What the player does: "Interact", "Skip". The document shows this. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Label;

	/** Which of the three sources this one resolved to, and what each needs. See FFolioGlyph. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FFolioGlyph Glyph;

	/** False when nothing is bound, so a document can hide the row rather than show an empty cap. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	bool bBound = false;
};

/**
 * The prompts one screen shows, resolved to whatever the player has actually bound.
 *
 * The reason this is a list a screen fills rather than something markup asks per element: a prompt
 * has to change when the player picks up a pad and when they rebind a key, and a document cannot
 * poll. Game code names the actions once, and the set re-resolves itself whenever the answer could
 * have moved.
 *
 * `Refresh` is cheap and safe to call often - on device change, after a rebind, on screen open.
 */
UCLASS(BlueprintType)
class FOLIO_API UFolioPromptSet : public UObject
{
	GENERATED_BODY()

public:
	/** Names an action to show. `MappingName` is Enhanced Input's, the same one a rebind uses. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Add(FName MappingName, const FString& Label);

	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Clear();

	/** Re-reads every prompt's key. Call after a rebind or a device change. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Refresh(ULocalPlayer* InLocalPlayer);

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	TArray<FFolioPrompt> Prompts;

private:
	/** What was asked for, kept so a refresh can resolve it again. */
	TArray<TPair<FName, FString>> Wanted;
};

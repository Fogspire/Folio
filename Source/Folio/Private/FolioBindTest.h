#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FolioBindTest.generated.h"

/**
 * A source for proving the view model bridge works, with no MVVM plugin needed.
 *
 * Deliberately a plain UObject: it exercises the reflection binding and the polled refresh, which
 * is the path a project gets when its source reports no changes of its own. A real
 * UMVVMViewModelBase differs only in updating by notification instead.
 */
UCLASS()
class UFolioBindTest : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	int32 Ticks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	float Health = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	bool bAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Status = TEXT("starting");

	/** Not marked visible, so it must not appear in the model. */
	UPROPERTY()
	int32 Hidden = 99;

	/** Advances the values, so a document bound to them visibly changes. */
	void Advance();
};

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FolioDocumentPath.generated.h"

/**
 * A path to a `.rml`, as its own type so the editor can give it a picker.
 *
 * A document is a path rather than an asset, deliberately - markup is text a person edits, not
 * something to import - but a bare FString field takes anything, including an absolute path that
 * works only on the machine it was typed on. Wrapping it is what lets the editor show the documents
 * that exist, in the directories they live in, instead of an empty text box.
 *
 * It reads and writes as a string everywhere else: assignment, comparison and the Blueprint node all
 * take an FString, and a level saved when this was a plain FString still loads.
 */
USTRUCT(BlueprintType)
struct FOLIO_API FFolioDocumentPath
{
	GENERATED_BODY()

	FFolioDocumentPath() = default;
	FFolioDocumentPath(const FString& InPath) : Path(InPath) {}

	/** Relative to the project, or under a `/folio/` mount for something the plugin ships. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FString Path;

	operator const FString&() const { return Path; }

	/** So *DocumentPath is a TCHAR* in a format string, exactly as *FString is. */
	const TCHAR* operator*() const { return *Path; }

	bool IsEmpty() const { return Path.IsEmpty(); }
	bool operator==(const FFolioDocumentPath& Other) const { return Path == Other.Path; }

	/**
	 * Reads a value that was saved when this property was a plain FString.
	 *
	 * Without it every already-placed component silently loses its document, which is a worse failure
	 * than the one the picker exists to prevent.
	 */
	bool SerializeFromMismatchedTag(const struct FPropertyTag& Tag, FStructuredArchive::FSlot Slot);
};

template<>
struct TStructOpsTypeTraits<FFolioDocumentPath> : public TStructOpsTypeTraitsBase2<FFolioDocumentPath>
{
	enum { WithStructuredSerializeFromMismatchedTag = true, WithIdenticalViaEquality = true };
};

/**
 * Everything to do with naming a document, in one place.
 *
 * A document is a path rather than an asset, which is the point - markup is text a person edits, not
 * something to import. The cost is that a path field takes anything, including an absolute one that
 * only works on the machine it was typed on, so this exists to make the editor's version of that
 * field behave like a picker instead of a text box.
 */
UCLASS()
class FOLIO_API UFolioDocumentPathLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Every `.rml` under the project and the plugin, as the paths Folio resolves.
	 *
	 * Named by `meta = (GetOptions = ...)` on every document path property, which turns the field into
	 * a searchable dropdown with no editor module and no customisation class.
	 *
	 * Scanned rather than cached: a document added while the editor is open should appear in the list
	 * without a restart, and the scan is a directory walk over a handful of files.
	 */
	UFUNCTION()
	static TArray<FString> GetDocumentOptions();

	/**
	 * The path Folio will actually open, or empty if it resolves to nothing.
	 *
	 * The same resolution the file interface does, exposed so the editor can answer "is this right"
	 * before anything tries to load it.
	 */
	UFUNCTION(BlueprintPure, Category = "Folio")
	static FString ResolveDocumentPath(const FString& DocumentPath);

	/** Whether a document path names a file that exists. */
	UFUNCTION(BlueprintPure, Category = "Folio")
	static bool DocumentExists(const FString& DocumentPath);

	/**
	 * Turns an absolute path into the portable form, and leaves anything already portable alone.
	 *
	 * A path typed or dragged in is absolute, and an absolute path is wrong the moment anyone else
	 * opens the level. Under the project it becomes `ui/screens/x.rml`; under the plugin it becomes
	 * `/folio/...`, which resolves from any content root.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	static FString MakeDocumentPathPortable(const FString& DocumentPath);

	/**
	 * Logs why a document did not load, naming what was tried.
	 *
	 * A path that resolves to nothing renders nothing, which is indistinguishable from a document that
	 * loaded and is laid out wrong - so the two have to be told apart here rather than by guessing.
	 * Lists what is available, because the answer is almost always a typo against something in the list.
	 */
	static void ReportMissingDocument(const FString& DocumentPath, const TCHAR* Context);
};

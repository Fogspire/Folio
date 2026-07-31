#pragma once

#include "Components/Widget.h"
#include "FolioDocumentPath.h"

#include "CoreMinimal.h"

#include "FolioWidget.generated.h"

class SFolioCanvas;

/** One requested binding, kept so it can be applied before the document loads and again on rebuild. */
USTRUCT()
struct FFolioViewModelBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FName ModelName;

	/** Strong: a view model handed to a widget and referenced nowhere else must not be collected. */
	UPROPERTY()
	TObjectPtr<UObject> Source;

	UPROPERTY()
	bool bPollEveryFrame = true;
};

/**
 * A document as a UMG widget, so it can sit in a game's existing widget tree.
 *
 * Folio replaces UMG for building UI; this exists for the boundary, not for building anything. A
 * game keeps whatever it already has - a HUD layout, a CommonUI stack, a menu flow - and puts a
 * document inside it, rather than porting the shell before the first screen exists.
 *
 * It also solves focus. CommonUI's action router asks its leaf-most activatable widget for a
 * GetDesiredFocusTarget() and, when it gets none it can use, focuses the game viewport instead -
 * silently taking focus off a document that had it. Returning one of these from that override hands
 * focus to the canvas properly, because UWidget::SupportsKeyboardFocus defers to the Slate widget
 * underneath and the canvas supports it:
 *
 *     UWidget* UMyScreen::GetDesiredFocusTarget() const override { return Document; }
 *
 * That is the whole integration, and it needs nothing from Folio but this class - which is why
 * Folio links no CommonUI and works the same in a project that has never heard of it.
 */
UCLASS(MinimalAPI, meta = (DisplayName = "Folio Document"))
class UFolioWidget : public UWidget
{
	GENERATED_BODY()

public:
	/**
	 * Document to load, relative to the project directory.
	 *
	 * A leading /folio/ resolves inside the plugin instead, which is how its own diagnostic
	 * documents are reachable from any project.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FFolioDocumentPath DocumentPath;

	/** Reported to Slate layout. A document is laid out to whatever size it ends up with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D DesiredSize = FVector2D(1920.0, 1080.0);

	/** Loads a different document, replacing whatever is up. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void LoadDocument(const FString& InDocumentPath);

	/**
	 * Binds a UObject's properties as a named data model, so a document can read them.
	 *
	 * This is the view model path, and it takes any UObject. A source implementing
	 * INotifyFieldValueChanged - which UMVVMViewModelBase does - updates the document as its fields
	 * change. One that does not is re-read once a frame instead, which costs work per bound field, so
	 * a view model is the better source when there is a choice.
	 *
	 * The document reads them as it would any model: <body data-model="player"> and {{ health }}.
	 *
	 * Callable at any point, including before the widget is built, which is the ordinary case in a
	 * Blueprint construction script. A document resolves its bindings when it loads, so this is the
	 * one call that must happen first - the widget defers loading until its bindings are in, and
	 * rebuilds the document if one is already up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API bool BindViewModel(FName ModelName, UObject* Source, bool bPollEveryFrame = true);

	/** Moves keyboard focus to the document. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void TakeFocus();

	/**
	 * Holds focus against anything that reassigns it.
	 *
	 * A last resort, for a document that is not reachable through a CommonUI focus target. Prefer
	 * the GetDesiredFocusTarget override: this one wins the argument rather than avoiding it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void SetKeepFocus(bool bKeepFocus);

	/** The canvas, once built. Null before construction and after release. */
	TSharedPtr<SFolioCanvas> GetCanvas() const { return Canvas; }

	FOLIO_API virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	FOLIO_API virtual const FText GetPaletteCategory() override;
#endif

protected:
	FOLIO_API virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** Applies every requested binding to the canvas. Must run before the document loads. */
	void ApplyBindings();

	UPROPERTY()
	TArray<FFolioViewModelBinding> Bindings;

	TSharedPtr<SFolioCanvas> Canvas;
};

#pragma once

#include "Components/Widget.h"
#include "FolioDocumentPath.h"

#include "CoreMinimal.h"

#include "FolioScale.h"

#include "FolioWidget.generated.h"

class SFolioCanvas;
class UTexture;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio",
		meta = (FolioSizeProperty = "DesiredSize"))
	FFolioDocumentPath DocumentPath;

	/** Reported to Slate layout. A document is laid out to whatever size it ends up with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	FVector2D DesiredSize = FVector2D(1920.0, 1080.0);

	/**
	 * Whether the document takes input at all.
	 *
	 * Turn it off for a HUD. A HUD is a readout drawn over a game that is still being played, and it
	 * must never be a focus target: Slate restores focus to a focusable widget when the window is
	 * reactivated, so a HUD that can hold focus takes it on alt-tab and the game stops receiving input
	 * with nothing in the log to say why. Off also makes the widget hit-test invisible, so clicks
	 * reach the game rather than being swallowed by a readout that does nothing with them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio")
	bool bInteractive = true;

	/**
	 * How much dp follows the viewport.
	 *
	 * 1 is Unreal's UI scale, so the document keeps the same fraction of the screen at every
	 * resolution - right for a menu. 0 is a constant pixel size. A HUD wants something in between:
	 * around 0.5 it grows with the window without inflating, and the corners still move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folio|Scale", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScaleFollow = 1.0f;

	/** A multiplier on the result. A HUD scale slider writes this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folio|Scale", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float Scale = 1.0f;

	/** Bounds on the result, so a 4K window does not have to draw a HUD twice the size. Zero for no bound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folio|Scale", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float MinScale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Folio|Scale", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float MaxScale = 0.0f;

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

	/**
	 * Writes one inline property on an element, by id, and marks the document dirty.
	 *
	 * For a value the game drives every frame from something a stylesheet cannot express - a spring, a
	 * signal that never settles - which is what UFolioHudMotion does through the same route. Anything
	 * that is really a state change belongs in RCSS as a class and a transition, and anything that is
	 * really data belongs in a view model; reach for this when it is neither.
	 *
	 * False if no document is up or nothing carries that id.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API bool SetElementProperty(const FString& ElementId, const FString& Property, const FString& Value);

	/**
	 * Hands a texture to this document under a name it can draw.
	 *
	 *     Widget->RegisterTexture(TEXT("minimap"), CaptureTarget);
	 *     <img src="folio://minimap"/>
	 *
	 * For anything a document cannot reach as a file: a scene capture behind a minimap, a paper doll,
	 * a rendered item. It works everywhere a source does - `img`, every decorator, `mask-image`.
	 *
	 * The widget releases the names it registered when it is torn down, so a screen cleans up after
	 * itself. **Names are global**, because RmlUi caches a texture against its source string and Folio
	 * has one render manager for the application: two documents naming `portrait` share whichever
	 * texture was registered last, and the swap is logged. Prefix by screen when that could happen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void RegisterTexture(FName Name, UTexture* Texture);

	/** Drops a name this widget registered. Called for all of them when the widget goes away. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void UnregisterTexture(FName Name);

	/** Turns input on or off on a document that is already up. See bInteractive. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void SetInteractive(bool bInInteractive);

	/** Applies the scale properties to a document that is already up. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	FOLIO_API void ApplyScale();

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

	/** Names this widget put in the registry, so they can be taken back out. */
	TSet<FName> OwnedTextures;

	TSharedPtr<SFolioCanvas> Canvas;
};

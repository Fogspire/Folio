#pragma once

#include "FolioDocumentHost.h"
#include "FolioScale.h"
#include "FolioNavigation.h"
#include "FolioUiSounds.h"

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

namespace Rml
{
	class Context;
	class ElementDocument;
}


/**
 * The entire Unreal-side surface: one widget acting as a dumb canvas.
 *
 * It reports a desired size, forwards input into the RmlUi context and emits geometry. It has
 * no children, takes no part in Slate layout and runs no Slate navigation. Treat it as an
 * HTML canvas: everything inside is RmlUi's, and Slate is only the drawing surface.
 *
 * Slate's own focus and navigation must never run for this content, which is why the widget
 * neither creates focusable children nor participates in Slate's navigation queries.
 */
class FOLIO_API SFolioCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFolioCanvas)
		: _DocumentPath()
		, _DesiredSize(FVector2D(1920.0, 1080.0))
		, _Interactive(true)
		, _ScaleFollow(1.0f)
		, _Scale(1.0f)
		, _MinScale(0.0f)
		, _MaxScale(0.0f)
		, _Supersample(1)
	{}
		/** Document to load, relative to the project directory. */
		SLATE_ARGUMENT(FString, DocumentPath)

		/** What the widget reports when its container asks. It fills whatever it is given. */
		SLATE_ARGUMENT(FVector2D, DesiredSize)

		/** False for a readout nobody clicks. See SetInteractive. */
		SLATE_ARGUMENT(bool, Interactive)

		/** How much dp follows the viewport. See Folio::ScaleRatio. */
		SLATE_ARGUMENT(float, ScaleFollow)

		/** A multiplier on the result. A HUD scale slider writes this. */
		SLATE_ARGUMENT(float, Scale)

		/** Bounds on the result. Zero for no bound. */
		SLATE_ARGUMENT(float, MinScale)
		SLATE_ARGUMENT(float, MaxScale)

		/** How many samples per pixel each way. One draws straight to the screen. See Supersample. */
		SLATE_ARGUMENT(int32, Supersample)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFolioCanvas() override;

	/** Replaces the current document. An empty path unloads without loading anything. */
	void LoadDocument(const FString& RelativePath);

	/**
	 * Rebuilds the current document from disk, putting focus and scroll back where they were.
	 *
	 * Heavier than ReloadStyleSheets and loses anything the DOM was holding, so hot reload only
	 * takes this path when the markup itself changed.
	 */
	void ReloadDocument();

	/** Re-reads every stylesheet, leaving the DOM, focus, scroll and data model alone. */
	void ReloadStyleSheets();

	/** Changes how dp resolves on a canvas that is already up. */
	void SetScale(float InFollow, float InScale, float InMinScale, float InMaxScale);

	/**
	 * Renders the document larger than the screen and scales the result down.
	 *
	 * A background's antialiasing is one pixel of coverage, which is exact and is what a browser
	 * does. It is also the least that can look smooth: a shallow edge - a slanted card, a long
	 * diagonal - advances a pixel every six or more, so the steps between are visible however
	 * accurate the blend is, and no amount of widening the skirt fixes that. Drawing at twice the
	 * size and filtering down gives four samples per pixel on every edge instead.
	 *
	 * Costs a render target of the viewport times this, squared, and one composite a frame. Text is
	 * a distance field and needs none of this, so a document that is mostly text gains nothing but
	 * the resample.
	 */
	void SetSupersample(int32 InSamples);

	/**
	 * Says the document has changed and must be updated and repainted.
	 *
	 * Only needed by game code that changes a document behind Folio's back. Everything Folio does
	 * itself - input, loading, resizing, a data model update - already marks itself.
	 *
	 * The reason is for `folio.Invalidation.Trace`, which is how "why does this document never idle"
	 * gets answered. Without it the trace can say a document is busy but not what is keeping it busy.
	 */
	void MarkDirty(const TCHAR* Reason = TEXT("game code"));

	Rml::Context* GetContext() const { return Host.GetContext(); }
	Rml::ElementDocument* GetDocument() const { return Host.GetDocument(); }

	/** The path the document was loaded from, which is also what its gather namespace derives from. */
	const FString& GetDocumentPath() const { return Host.GetDocumentPath(); }

	/** The document and its context, shared with the world-space host. */
	FFolioDocumentHost& GetHost() { return Host; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual bool SupportsKeyboardFocus() const override { return bInteractive; }

	//
	// Input. Forwarded straight into the RmlUi context: Slate's own focus and navigation
	// never run for this content, so the widget is the only thing Slate sees.
	//
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/**
	 * The second press of a fast pair.
	 *
	 * Slate sends this instead of a button-down once two clicks land inside the double-click time,
	 * so without it every other click on a stepper is dropped and the control reads as though it has
	 * a cooldown. A document that wants double-clicks reads the dblclick RmlUi raises from the pair.
	 */
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& CharacterEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& AnalogEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& FocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& FocusEvent) override;

	/**
	 * Focuses something navigable, if nothing in the document is focused yet.
	 *
	 * RmlUi navigates from the focused element, and a document that has just been shown focuses
	 * itself rather than anything in it. A player who reaches for the d-pad first then presses
	 * directions at a document with nowhere to move from, and nothing happens until they press
	 * confirm - which is not a thing anyone would guess.
	 *
	 * The first element in tab order, or the one marked `autofocus`, which is RmlUi's own answer to
	 * where a document starts.
	 */
	void FocusFirstElement();

	/**
	 * The player picked up something else.
	 *
	 * Two things go wrong without this, and both of them point the player at the wrong control.
	 * The hover chain survives, because the cursor has not moved and RmlUi has no reason to think
	 * otherwise, so a mouse highlight sits there competing with the focus ring. And the ring itself
	 * is usually invisible, because whatever holds focus was focused by a click.
	 */
	void OnDeviceChanged(Folio::EInputDevice Device);

	Folio::EInputDevice LastDevice = Folio::EInputDevice::Unknown;

	/** How far the pointer has travelled since the pad was last used. */
	float MouseTravel = 0.0f;

	/** What the player last touched. Documents swap button glyphs on it. */
	Folio::EInputDevice GetLastInputDevice() const;

	/**
	 * Binds a UObject's properties as a named data model.
	 *
	 * The view model path. Works with any UObject; a source implementing INotifyFieldValueChanged -
	 * which UMVVMViewModelBase does - updates by notification, and one that does not can be polled
	 * instead. Repeatable with different names, so a screen can bind several sources.
	 */
	bool BindObjectModel(const FString& ModelName, UObject* Source, bool bPollEveryFrame = true);

	/**
	 * Whether game code has already bound this model name.
	 *
	 * A document's bindings resolve when it loads, and RmlUi refuses a second model under a name it
	 * already has, so the order matters: bind before loading, and design-time mock data stays out of
	 * the way of anything real.
	 */
	bool HasObjectModel(const FString& ModelName) const { return Host.HasObjectModel(ModelName); }

	/** What the last tick laid out against: context size in dp, the dp ratio, and context-to-local. */
	void GetScaleReport(FIntPoint& OutContextSize, float& OutDpRatio, float& OutContextToLocal) const;

	/**
	 * The back action, when nothing in the document wanted it.
	 *
	 * A screen that takes input only - which is what a menu usually wants - leaves the player
	 * controller receiving nothing, so a game cannot bind escape or the pad's back button itself.
	 * Without this the only way to close such a screen would be a control inside it, and a menu whose
	 * back button is the only way out is a menu players complain about.
	 */
	FSimpleDelegate OnBack;

	/** Every live canvas, so the input pre-processor can tell whether the focused widget is one. */
	static const TArray<SFolioCanvas*>& GetLiveCanvases();

	/**
	 * Asks to keep keyboard focus even when something else takes it away.
	 *
	 * CommonUI's action router owns focus in any project that uses it. When its input config is
	 * reapplied - which changing the input mode or touching a gamepad both do - it looks for a focus
	 * target on its leaf-most activatable widget, finds none, and focuses the game viewport, taking
	 * focus off a document that had it. Nothing is logged as an error; input simply stops arriving.
	 *
	 * With this set, the pre-processor takes focus back when an event arrives and a canvas that
	 * wanted focus does not have it. Off by default: a document embedded in a game's own widget tree
	 * should let that tree decide, and the right answer there is a CommonUI focus target pointing at
	 * the canvas rather than a fight over it.
	 */
	void SetKeepFocus(bool bInKeepFocus) { bKeepFocus = bInKeepFocus; }
	bool WantsFocus() const { return bInteractive && bKeepFocus; }

	/**
	 * Whether the document takes input at all.
	 *
	 * A HUD is not a screen. It is a readout drawn over a game that is still being played, and it must
	 * never be a focus target: Slate restores focus to a focusable widget when the window is
	 * reactivated, so a HUD that can hold focus takes it on alt-tab and the game stops receiving
	 * input with nothing in the log to say why.
	 *
	 * Off makes the widget hit-test invisible and unfocusable, so the mouse and the keyboard pass
	 * through to whatever is behind. Everything else - painting, motion, data binding, hot reload -
	 * is unaffected.
	 */
	void SetInteractive(bool bInInteractive);
	bool IsInteractive() const { return bInteractive; }

	/**
	 * Gamepad entry points for the input pre-processor, which has an event but no geometry.
	 *
	 * Safe because key and analog handling never consults the geometry: a key means the same thing
	 * wherever the widget sits. Only the pointer handlers need it, and none of those come this way.
	 */
	bool RouteGamepadKeyDown(const FKeyEvent& KeyEvent);
	bool RouteGamepadKeyUp(const FKeyEvent& KeyEvent);
	bool RouteGamepadAnalog(const FAnalogInputEvent& AnalogEvent);

private:
	/** Sends a navigation key both ways, since RmlUi expects a press to be released. */
	bool SendNavigation(Rml::Input::KeyIdentifier Key, int32 Modifiers);

	/** Records the device, which the module then publishes to every document. */
	void NoteDevice(Folio::EInputDevice Device);

	/** True while a text field has focus, so letters are text rather than directions. */
	bool IsTakingText() const { return Host.IsTakingText(); }

	/** Whether this key belongs to the focused document, and so must not reach the game behind it. */
	bool ClaimsKey(const FKey& Key) const;

public:
	/**
	 * A key the document had no meaning for.
	 *
	 * While a screen holds focus the game's own bindings do not fire, because Slate routes to the
	 * focused widget and a player controller is not it. A menu button that should close the menu is
	 * the obvious casualty. This is the seam: navigation, confirm and back never reach it, and
	 * everything else does.
	 */
	DECLARE_DELEGATE_OneParam(FOnUnclaimedKey, FKey);
	FOnUnclaimedKey OnUnclaimedKey;

private:

	FFolioDocumentHost Host;
	Folio::FAnalogNavigator Analog;
	Folio::FUiSoundListener Sounds;
	bool bKeepFocus = false;

	/** Watches the module for the device changing, so focus can follow it. */
	FDelegateHandle DeviceChanged;
	bool bInteractive = true;

	/** Screen space to the context's own pixel space. */
	FVector2f ToContextSpace(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const;

	/** RmlUi sizes in whole pixels, so a fractional widget size has to be resolved somewhere. */
	void UpdateContextSize(const FGeometry& AllottedGeometry);

	/** The dp ratio this canvas lays out against, after the mode and the bounds. */
	float ScaleFor(const FGeometry& AllottedGeometry) const;

	/** How much one context unit is worth in Slate's local units. One while dp follows Slate. */
	float ContextToLocal(const FGeometry& AllottedGeometry) const;

	float ScaleFollow = 1.0f;
	float Scale = 1.0f;
	float MinScale = 0.0f;
	float MaxScale = 0.0f;

	/** One to draw straight to the screen. */
	int32 Supersample = 1;

	/** Where a supersampled document is drawn before it is scaled down. Mutable: painting is const. */
	mutable TStrongObjectPtr<class UTextureRenderTarget2D> Target;

	/** Names that target for Slate. Rebuilt with it, because a brush caches the resource. */
	mutable FSlateBrush TargetBrush;

	/** Resolved on the game thread, so drawing it does not go through Slate's dynamic resources. */
	mutable FSlateResourceHandle TargetHandle;

	/** True while the offscreen path is in use, so input and painting agree about the size. */
	bool SupersamplingNow() const;

	FVector2D DesiredSize = FVector2D(1920.0, 1080.0);

	/** Mutable: Slate's paint is const, and RmlUi's render pass is not. */
	mutable FIntPoint ContextSize = FIntPoint::ZeroValue;

	/**
	 * Where the cursor last was, in the coordinates RmlUi received.
	 *
	 * A captured viewport sends a mouse move every frame whether the cursor moved or not, so without
	 * this a document is never idle and the invalidation never engages.
	 */
	TOptional<FIntPoint> LastMousePosition;
	int32 LastMouseModifiers = 0;
};

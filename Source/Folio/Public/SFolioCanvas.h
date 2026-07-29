#pragma once

#include "FolioInputState.h"
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
	{}
		/** Document to load, relative to the project directory. */
		SLATE_ARGUMENT(FString, DocumentPath)

		/** What the widget reports when its container asks. It fills whatever it is given. */
		SLATE_ARGUMENT(FVector2D, DesiredSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SFolioCanvas() override;

	/** Replaces the current document. An empty path unloads without loading anything. */
	void LoadDocument(const FString& RelativePath);

	Rml::Context* GetContext() const { return Context; }
	Rml::ElementDocument* GetDocument() const { return Document; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual bool SupportsKeyboardFocus() const override { return true; }

	//
	// Input. Forwarded straight into the RmlUi context: Slate's own focus and navigation
	// never run for this content, so the widget is the only thing Slate sees.
	//
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& CharacterEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& AnalogEvent) override;
	virtual void OnFocusLost(const FFocusEvent& FocusEvent) override;

	/** What the player last touched. Documents swap button glyphs on it. */
	Folio::EInputDevice GetLastInputDevice() const;

	/** Every live canvas, so the input pre-processor can tell whether the focused widget is one. */
	static const TArray<SFolioCanvas*>& GetLiveCanvases();

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
	bool IsTakingText() const;

	Folio::FAnalogNavigator Analog;
	Folio::FUiSoundListener Sounds;
	FFolioInputModel InputModel;

	/** Screen space to the context's own pixel space. */
	FVector2f ToContextSpace(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const;

	void DestroyContext();

	/** Mounts ui/mock/<model>.json if the document declares a model, so bindings resolve. */
	void MountDataModel(const FString& RelativePath);

	/** RmlUi sizes in whole pixels, so a fractional widget size has to be resolved somewhere. */
	void UpdateContextSize(const FGeometry& AllottedGeometry);

	Rml::Context* Context = nullptr;
	Rml::ElementDocument* Document = nullptr;

	FString PendingDocumentPath;
	/** Rebuilt with the context: a model cannot be added once a document is loaded. */
	TUniquePtr<class FFolioDataModel> DataModel;
	FVector2D DesiredSize = FVector2D(1920.0, 1080.0);

	/** Mutable: Slate's paint is const, and RmlUi's render pass is not. */
	mutable FIntPoint ContextSize = FIntPoint::ZeroValue;
};

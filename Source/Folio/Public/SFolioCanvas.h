#pragma once

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

private:
	void DestroyContext();

	/** RmlUi sizes in whole pixels, so a fractional widget size has to be resolved somewhere. */
	void UpdateContextSize(const FGeometry& AllottedGeometry);

	Rml::Context* Context = nullptr;
	Rml::ElementDocument* Document = nullptr;

	FString PendingDocumentPath;
	FVector2D DesiredSize = FVector2D(1920.0, 1080.0);

	/** Mutable: Slate's paint is const, and RmlUi's render pass is not. */
	mutable FIntPoint ContextSize = FIntPoint::ZeroValue;
};

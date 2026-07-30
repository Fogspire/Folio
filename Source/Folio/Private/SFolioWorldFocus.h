#pragma once

#include "Widgets/SLeafWidget.h"

class FFolioDocumentHost;

/**
 * An invisible widget that exists only to hold Slate's keyboard focus for a world document.
 *
 * A document in the world has no widget, so nothing delivers characters to it. Deriving them from key
 * events instead is not an option worth taking: keyboard layout, modifiers, dead keys and IME all live
 * in the path between a key and a character, and none of that is reproducible from a key code. So a
 * real widget takes focus and forwards what Slate produces.
 *
 * Focus is held only while a text field in the document has it. Everything else - movement, weapons,
 * the pause key - keeps working, because an unconsumed key falls through to the viewport exactly as it
 * would with no widget here at all.
 */
class SFolioWorldFocus final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFolioWorldFocus) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FFolioDocumentHost* InHost);

	/** Puts the sink in the viewport and gives it focus. Null host removes it again. */
	static void Attach(FFolioDocumentHost* InHost);
	static void Detach(const FFolioDocumentHost* InHost);

	//~ Begin SWidget
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& CharacterEvent) override;
	//~ End SWidget

private:
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&,
		int32 LayerId, const FWidgetStyle&, bool) const override
	{
		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	FFolioDocumentHost* Host = nullptr;
};

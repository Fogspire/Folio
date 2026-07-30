#pragma once

#include "FolioNavigation.h"

#include "Widgets/SLeafWidget.h"

class FFolioDocumentHost;

/**
 * An invisible widget that holds Slate's input focus for a world document.
 *
 * A document in the world has no widget, so nothing routes input to it. Characters are the reason it
 * has to be a real widget rather than a poll: keyboard layout, modifiers, dead keys and IME all live
 * between a key and a character, and none of that is reproducible from a key code.
 *
 * It handles navigation on the same terms as the screen canvas, so a document on a monitor in the
 * world answers the d-pad, the arrows, WASD and IJKL exactly as a menu does. Two levels of focus:
 * while only a text field has it, unclaimed keys still fall through and the player can walk away
 * mid-word; while the document has been focused outright, directional input belongs to it and the
 * game behind stops receiving it.
 */
class SFolioWorldFocus final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFolioWorldFocus) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FFolioDocumentHost* InHost, bool bInNavigates);

	/** Puts the sink in the viewport and gives it focus. */
	static void Attach(FFolioDocumentHost* InHost, bool bNavigates);
	static void Detach(const FFolioDocumentHost* InHost);

	/**
	 * Gamepad input, handed over by the input pre-processor rather than by Slate.
	 *
	 * Same reason the canvas needs it: CommonUI registers a pre-processor that takes gamepad events
	 * before any widget sees them, so holding focus is not enough on its own.
	 */
	static bool RouteGamepadKeyDown(const FKeyEvent& KeyEvent);
	static bool RouteGamepadKeyUp(const FKeyEvent& KeyEvent);
	static bool RouteGamepadAnalog(const FAnalogInputEvent& AnalogEvent);

	/** Whether a world document is currently holding focus at all. */
	static bool IsActive();

	/**
	 * Takes focus back if something else has it while the sink is up.
	 *
	 * The game viewport reassigns focus to itself on its own, and CommonUI's action router does the same
	 * whenever it reapplies its input config, so a document that asked for focus can lose it with nothing
	 * going wrong that a log would show. The symptom is a field that accepts some keys while the rest walk
	 * the player. Taken back when input arrives rather than every frame, which settles in one event.
	 */
	static void ReclaimFocus(FSlateApplication& SlateApp, int32 UserIndex);

	//~ Begin SWidget
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
	virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& Geometry, const FCharacterEvent& CharacterEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& AnalogEvent) override;
	virtual void OnFocusLost(const FFocusEvent& FocusEvent) override;
	//~ End SWidget

private:
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&,
		int32 LayerId, const FWidgetStyle&, bool) const override
	{
		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

	/** Whether a key belongs to the document rather than to the game behind it. */
	bool ClaimsKey(const FKey& Key) const;

	/** Down and up together, which is what RmlUi wants for a navigation press. */
	bool SendNavigation(Rml::Input::KeyIdentifier Key, int32 Modifiers);

	bool IsTakingText() const;

	FFolioDocumentHost* Host = nullptr;

	/** False when the document only has focus because a text field does. See the class comment. */
	bool bNavigates = false;

	Folio::FAnalogNavigator Analog;

	/**
	 * Keys whose press this took, so only their release is taken too.
	 *
	 * Focus can arrive between a press and its release: the press reaches the game, and consuming the
	 * release would leave Enhanced Input holding an action nothing will ever complete. The player's pawn
	 * runs off with the key already let go and no way to stop it.
	 */
	TSet<FKey> Claimed;
};

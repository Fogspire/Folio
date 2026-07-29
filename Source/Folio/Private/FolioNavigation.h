#pragma once

#include "FolioRmlUi.h"

#include "FolioCore/FolioInputState.h"

#include "Delegates/Delegate.h"
#include "InputCoreTypes.h"

/**
 * Directional input, from whatever produced it.
 *
 * RmlUi navigates on KI_UP/DOWN/LEFT/RIGHT and activates on KI_RETURN. Everything a player might
 * use to mean "up" is funnelled into those here, so a document never learns whether it was a
 * d-pad, a stick, the arrow keys, WASD or IJKL - and all of them work at once, which is the thing
 * the phase gate actually asks for.
 */
namespace Folio
{
	const TCHAR* DescribeDevice(EInputDevice Device);

	/** Which device a key belongs to. */
	EInputDevice DeviceForKey(const FKey& Key);

	/**
	 * The navigation key an Unreal key means, or KI_UNKNOWN.
	 *
	 * Covers the gamepad's d-pad and face buttons and all three keyboard schemes. Ordinary keys
	 * are not handled here - ToRmlKey does those, and text entry has to keep working while W, A,
	 * S and D also navigate.
	 */
	Rml::Input::KeyIdentifier ToNavigationKey(const FKey& Key);

	/**
	 * Whether the arrow keys navigate.
	 *
	 * Arrows reach RmlUi through ToRmlKey rather than ToNavigationKey, because they mean
	 * something inside a text field too, so switching them off is the caller's job.
	 *
	 * All four schemes are console variables so a project can pick without rebuilding:
	 * folio.Nav.Arrows, folio.Nav.WASD, folio.Nav.IJKL, folio.Nav.Gamepad, set from
	 * DefaultEngine.ini like any other. A game that binds WASD to movement while a HUD is up
	 * wants that off; one with no gamepad support wants it gone from the glyph swapping too.
	 */
	bool ArrowsNavigate();

	/**
	 * Which gamepad face button confirms, the other one meaning back.
	 *
	 * Backed by folio.Nav.SwapConfirmBack so a platform default can be set from DefaultEngine.ini
	 * per target, and SetConfirmButton lets a settings screen change it at runtime. Nothing here
	 * knows about any particular console: a port decides for itself and calls this.
	 */
	EConfirmButton ConfirmButton();
	void SetConfirmButton(EConfirmButton Button);

	/** Fires when it changes, from either the console variable or the setter. */
	FSimpleMulticastDelegate& OnConfirmButtonChanged();

	/**
	 * Turns a held analog stick into repeated directional presses.
	 *
	 * A stick reports a position, not an event, so without this it either does nothing or fires
	 * every frame. Held past the deadzone it emits once, waits, then repeats - the same shape as a
	 * keyboard's auto-repeat, and the timings are the ones the brief names.
	 */
	class FAnalogNavigator
	{
	public:
		/** Records an axis value. Called from the widget's analog handler. */
		void SetAxis(const FKey& Key, float Value);

		/**
		 * Advances the clock and returns the direction to emit this frame, or KI_UNKNOWN.
		 *
		 * One direction at a time: a stick held diagonally would otherwise navigate twice per
		 * repeat, and which of the two arrives first would depend on the frame.
		 */
		Rml::Input::KeyIdentifier Advance(float DeltaSeconds);

		/** Forgotten when focus leaves, so a stick held across a screen change does not carry. */
		void Reset();

		/** Past this the stick counts as pushed. Below it, drift and a resting stick read as zero. */
		static constexpr float Deadzone = 0.5f;
		/** Held, the first repeat waits this long, so a deliberate nudge moves exactly one item. */
		static constexpr float InitialDelay = 0.4f;
		/** And every one after that. */
		static constexpr float RepeatInterval = 0.12f;

	private:
		float X = 0.0f;
		float Y = 0.0f;

		Rml::Input::KeyIdentifier Held = Rml::Input::KI_UNKNOWN;
		float Countdown = 0.0f;
	};
}

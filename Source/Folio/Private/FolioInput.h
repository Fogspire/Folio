#pragma once

#include "FolioRmlUi.h"

struct FKey;
class FModifierKeysState;

/**
 * Translation between Unreal's input and RmlUi's.
 *
 * Deliberately thin. Directional input is remapped to KI_UP/DOWN/LEFT/RIGHT by the input
 * layer rather than here, so gamepad, arrows, WASD and IJKL all arrive as the same four keys
 * and RmlUi never learns what produced them.
 */
namespace Folio
{
	/** RmlUi's modifier bitfield, from Slate's modifier state. */
	int32 ToRmlModifiers(const FModifierKeysState& Modifiers);

	/**
	 * RmlUi's key identifier for an Unreal key, or KI_UNKNOWN.
	 *
	 * Only what a UI needs: navigation, activation, editing and text entry. A key with no
	 * meaning to RmlUi is better left unmapped than mapped to something arbitrary.
	 */
	Rml::Input::KeyIdentifier ToRmlKey(const FKey& Key);

	/** RmlUi's button index, or -1 for a button it does not model. */
	int32 ToRmlMouseButton(const FKey& Key);
}

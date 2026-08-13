#pragma once

#include "CoreMinimal.h"

namespace Folio
{
	/**
	 * Whether a pad is what the player has in hand, across every document in the process.
	 *
	 * Folio learns the device from input its own canvas receives. That covers a menu and never covers
	 * a HUD: a canvas that takes no focus sees no input at all, so a pad picked up during play leaves
	 * every world prompt still drawing a keyboard key. A game tells Folio the answer from whatever
	 * does watch input everywhere - CommonInput's pre-processor, say - and this is how.
	 *
	 * Only the pad boundary, because that is the whole of what a game can improve on: which of a mouse
	 * and a keyboard is in use is a finer question, Folio answers it from the event it saw, and that
	 * answer decides whether a cursor is on screen. Setting the pad off leaves the keyboard.
	 *
	 * A bool rather than Folio's own device enum: that type lives in FolioCore, which is private to
	 * this plugin, so a public header cannot name it.
	 */
	FOLIO_API bool IsGamepadInput();

	FOLIO_API void SetGamepadInput(bool bGamepad);
}

#pragma once

#include "CoreMinimal.h"

class SFolioCanvas;

// The debugger is development tooling: a player able to open the element tree over the game is not
// something to ship. The lib is linked in every configuration, but nothing here references it in
// Shipping so it drops out.
#if !UE_BUILD_SHIPPING

/**
 * Rml::Debugger, which is RmlUi's own element inspector: element tree, computed style, event log
 * and a live view of every data model in the context.
 *
 * There is one debugger per process and it lives inside the context it is inspecting, so attaching
 * to a second canvas means detaching from the first. RmlUi releases it when its host context is
 * destroyed, which is why every entry point here re-checks that the host canvas is still alive
 * rather than trusting the stored pointer.
 */
namespace Folio::Debug
{
	/** Attaches to a canvas's context, replacing any previous attachment. Idempotent per canvas. */
	bool Attach(SFolioCanvas* Canvas);

	/** Drops the attachment. Safe at any point, including after RmlUi has shut down. */
	void Detach();

	void SetVisible(bool bVisible);
	bool IsVisible();

	/** The canvas being inspected, or null if nothing is attached or the host has since died. */
	SFolioCanvas* Host();
}

#endif

#pragma once

#include "CoreMinimal.h"

/**
 * What the interface reports for a game to make a noise about.
 *
 * Kept clear of RmlUi so a game module can include it: everything else that carries these signals
 * is a header a consumer cannot compile against, and a delegate nobody can reach is no delegate.
 */
namespace Folio
{
	enum class EUiSound : uint8
	{
		/** Focus moved to a different element. */
		Focus,
		/** A focused element was activated. */
		Confirm,
		/**
		 * Back was pressed and something in the document acted on it.
		 *
		 * Not reported for a back the document ignored: whether anything happens then is the host's
		 * answer to give, and a screen with nowhere to go back to must not sound as if it had.
		 */
		Back,
		/** An activation that went nowhere, because the element is disabled. */
		Invalid,
	};

	FOLIO_API const TCHAR* DescribeUiSound(EUiSound Sound);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnUiSound, EUiSound);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTextRevealSound, const FString& /* SoundName */);

	/** Whether Folio is loaded. False after it has gone, when unsubscribing must not load it again. */
	FOLIO_API bool AreSoundEventsAvailable();

	/** Fires on focus change, confirm, back and invalid, for whoever wants to play a sound. */
	FOLIO_API FOnUiSound& OnUiSound();

	/**
	 * Fires as a `<text>` reveals a character, naming whatever its `reveal-sound` names.
	 *
	 * At most once a frame however fast the text is typing.
	 */
	FOLIO_API FOnTextRevealSound& OnTextRevealSound();
}

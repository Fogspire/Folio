#pragma once

#include "CoreMinimal.h"

/**
 * Speaking out loud, without a screen reader running.
 *
 * A player on Windows who needs a menu read to them either runs NVDA - in which case the
 * accessibility tree is all they need and none of this is used - or expects the game to read itself.
 * Consoles have no third option at all: there is no separate screen reader to run, so a game either
 * self-voices or is silent.
 *
 * Windows only for now, through SAPI, which ships with the operating system and needs no third-party
 * library and no shipped voice data. Everywhere else this reports unavailable and the toggle that
 * drives it stays off.
 */
namespace Folio
{
	class FSpeech
	{
	public:
		/** Whether this platform can speak at all. */
		static bool IsAvailable();

		/** Creates the voice. Idempotent; false means this platform or this machine cannot. */
		static bool Start();

		/** Releases it. Anything mid-sentence stops. */
		static void Stop();

		static bool IsRunning();

		/**
		 * Says something.
		 *
		 * Interrupting is the right default for a focus change - a player who has moved on does not
		 * want to hear where they were - and the wrong one for an announcement, which is why it is a
		 * parameter rather than a policy.
		 */
		static void Say(const FString& Text, bool bInterrupt);

		/** -10 to 10, zero being the voice's own speed. Screen reader users routinely run this high. */
		static void SetRate(int32 Rate);
		static int32 GetRate();

		/** 0 to 100. */
		static void SetVolume(int32 Volume);
		static int32 GetVolume();
	};
}

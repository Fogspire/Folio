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
 * One back end ships, SAPI, because it is part of Windows and needs no third-party library and no
 * voice data of its own. Every other platform's speech API sits behind an SDK this plugin cannot see,
 * so those arrive through `SetBackend` from a module that can. With neither, this reports unavailable
 * and the setting that drives it stays off.
 */
namespace Folio
{
	/**
	 * One platform's way of talking.
	 *
	 * Five functions, and the only one with a rule attached is `Say`: a queued line waits for whatever
	 * is already being spoken, an interrupting one drops it. A back end with no queue of its own should
	 * speak immediately rather than drop the line - a reader that loses sentences is worse than one
	 * that talks over itself.
	 *
	 * Installed before the reader is turned on, which in practice means from a module's startup.
	 */
	class ISpeechBackend
	{
	public:
		virtual ~ISpeechBackend() = default;

		/** Acquires the voice. False means this machine cannot, which turns the setting back off. */
		virtual bool Start() = 0;

		/** Releases it, and stops anything mid-sentence. */
		virtual void Stop() = 0;

		virtual void Say(const FString& Text, bool bInterrupt) = 0;

		/** Drops whatever is queued and stops talking, without releasing the voice. */
		virtual void Silence() = 0;

		/** -10 to 10 and 0 to 100, already clamped. Both are pushed again on every Start. */
		virtual void SetRate(int32 Rate) = 0;
		virtual void SetVolume(int32 Volume) = 0;
	};

	class FSpeech
	{
	public:
		/**
		 * Installs the back end this platform speaks through, replacing whatever was there.
		 *
		 * Anything running is stopped first, so a swap mid-session is a change of voice rather than two
		 * of them. Passing nothing removes the back end, which is how a test puts the machine back.
		 */
		static FOLIO_API void SetBackend(TUniquePtr<ISpeechBackend> Backend);

		/** Whether this platform can speak at all. */
		static FOLIO_API bool IsAvailable();

		/** Creates the voice. Idempotent; false means this platform or this machine cannot. */
		static FOLIO_API bool Start();

		/** Releases it. Anything mid-sentence stops. */
		static FOLIO_API void Stop();

		static FOLIO_API bool IsRunning();

		/**
		 * Says something.
		 *
		 * Interrupting is the right default for a focus change - a player who has moved on does not
		 * want to hear where they were - and the wrong one for an announcement or for one line of a
		 * read-all, which is why it is a parameter rather than a policy.
		 */
		static FOLIO_API void Say(const FString& Text, bool bInterrupt);

		/** Drops whatever is queued. What a player pressing anything during a read-all expects. */
		static FOLIO_API void Silence();

		/** -10 to 10, zero being the voice's own speed. Screen reader users routinely run this high. */
		static FOLIO_API void SetRate(int32 Rate);
		static FOLIO_API int32 GetRate();

		/** 0 to 100. */
		static FOLIO_API void SetVolume(int32 Volume);
		static FOLIO_API int32 GetVolume();
	};
}

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
 *
 * **The queue lives here, not in the back end.** A voice that is handed everything as it happens
 * cannot be un-handed it, and a setting a player is holding a key on produces one line per frame - so
 * a rate slider left for two seconds becomes a minute of a voice reading numbers that stopped being
 * true before it started. Holding the queue is what makes a line droppable.
 */
namespace Folio
{
	/** How a line behaves against whatever is already being said. */
	enum class ESpeechPriority : uint8
	{
		/**
		 * Waits its turn, and is dropped if it waits too long.
		 *
		 * Everything that reports a state rather than a stream. A line about a value the player
		 * changed four seconds and forty presses ago is not worth hearing.
		 */
		Polite,
		/** Waits its turn and is never dropped. One line of something the player asked to have read. */
		Sequence,
		/** Drops the queue and cuts off what is speaking. Moving focus is this. */
		Assertive,
	};

	/**
	 * One platform's way of talking.
	 *
	 * `Speak` says one line **now**, replacing anything in flight - it must never queue, because the
	 * queue above it is the thing that can drop a line that has stopped being worth saying. `IsSpeaking`
	 * is what paces that queue; a back end that cannot answer it should return false, which degrades to
	 * each line cutting off the one before rather than to silence.
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

		virtual void Speak(const FString& Text) = 0;

		/** Stops talking without releasing the voice. */
		virtual void Silence() = 0;

		virtual bool IsSpeaking() const = 0;

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
		 * Queues a line.
		 *
		 * `Channel` is what makes a value that keeps changing survivable: a later line on the same
		 * channel **replaces** an earlier one that has not started yet, so holding a key on a slider
		 * leaves one pending number rather than forty. Lines with no channel never replace anything.
		 */
		static FOLIO_API void Say(const FString& Text, ESpeechPriority Priority,
			FName Channel = NAME_None);

		/** Drops the queue and stops talking. What pressing anything during a read-all expects. */
		static FOLIO_API void Silence();

		/** Moves the queue on. Called for you while the reader is running. */
		static FOLIO_API void Pump();

		/** How many lines are waiting, not counting the one being spoken. */
		static FOLIO_API int32 NumQueued();

		/**
		 * How long a polite line may wait before it is dropped unsaid, in seconds. Zero never drops.
		 *
		 * A backlog is not just slow, it is wrong: by the time it is read the screen has moved on, and
		 * the player is being told about a state that no longer exists.
		 */
		static FOLIO_API void SetStaleAfter(float Seconds);
		static FOLIO_API float GetStaleAfter();

		/** -10 to 10, zero being the voice's own speed. Screen reader users routinely run this high. */
		static FOLIO_API void SetRate(int32 Rate);
		static FOLIO_API int32 GetRate();

		/** 0 to 100. */
		static FOLIO_API void SetVolume(int32 Volume);
		static FOLIO_API int32 GetVolume();
	};
}

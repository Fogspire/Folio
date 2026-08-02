#include "FolioSpeech.h"

#include "FolioModule.h"

#include "Containers/Ticker.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <sapi.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	/**
	 * The voice, and whether this process was the one that started COM.
	 *
	 * Held for as long as the reader is on rather than made per sentence: creating one takes long
	 * enough to be heard as a stutter between menu items, which is exactly where it would be used.
	 */
	class FSapiBackend final : public Folio::ISpeechBackend
	{
	public:
		virtual ~FSapiBackend() override { FSapiBackend::Stop(); }

		virtual bool Start() override
		{
			if (Voice)
			{
				return true;
			}

			// Unreal has usually initialised COM on this thread already. RPC_E_CHANGED_MODE means it did
			// so with the other threading model, which is not a failure for an in-process object.
			const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			bOwnsCom = SUCCEEDED(ComResult);

			ISpVoice* Created = nullptr;
			const HRESULT Result = CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL,
				__uuidof(ISpVoice), (void**)&Created);

			if (FAILED(Result) || !Created)
			{
				UE_LOG(LogFolio, Warning,
					TEXT("no speech voice is available on this machine (0x%08x), so the screen reader "
						"cannot speak. A player running NVDA or Narrator is unaffected: that path reads "
						"the accessibility tree instead."),
					(uint32)Result);

				Release();
				return false;
			}

			Voice = Created;
			return true;
		}

		virtual void Stop() override
		{
			if (Voice)
			{
				Silence();
			}

			Release();
		}

		virtual void Speak(const FString& Text) override
		{
			if (!Voice)
			{
				return;
			}

			// Asynchronous, and purging first: the queue above this decides what waits, so nothing is
			// ever handed to SAPI that could still need dropping.
			Voice->Speak(*Text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
		}

		virtual void Silence() override
		{
			// An empty string with PURGEBEFORESPEAK is SAPI's way of saying stop talking now.
			if (Voice)
			{
				Voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
			}
		}

		virtual bool IsSpeaking() const override
		{
			if (!Voice)
			{
				return false;
			}

			SPVOICESTATUS Status = {};
			return SUCCEEDED(Voice->GetStatus(&Status, nullptr))
				&& Status.dwRunningState == SPRS_IS_SPEAKING;
		}

		virtual void SetRate(const int32 Rate) override
		{
			if (Voice)
			{
				Voice->SetRate(Rate);
			}
		}

		virtual void SetVolume(const int32 Volume) override
		{
			if (Voice)
			{
				Voice->SetVolume((USHORT)Volume);
			}
		}

	private:
		void Release()
		{
			if (Voice)
			{
				Voice->Release();
				Voice = nullptr;
			}

			if (bOwnsCom)
			{
				CoUninitialize();
				bOwnsCom = false;
			}
		}

		ISpVoice* Voice = nullptr;
		bool bOwnsCom = false;
	};

	TUniquePtr<Folio::ISpeechBackend> MakeDefaultBackend()
	{
		return MakeUnique<FSapiBackend>();
	}
}

#else

namespace
{
	TUniquePtr<Folio::ISpeechBackend> MakeDefaultBackend()
	{
		return nullptr;
	}
}

#endif

namespace
{
	/** A line waiting to be said. */
	struct FUtterance
	{
		FString Text;

		/** A later line on the same channel replaces this one while it is still waiting. */
		FName Channel;

		/** When it was queued, so a line nobody could still want is dropped rather than read. */
		double At = 0.0;

		/** False for one line of something the player asked to have read. Those wait as long as it takes. */
		bool bPerishable = true;
	};

	/**
	 * The installed back end, the queue, and the settings that outlive both.
	 *
	 * Rate and volume live here rather than in the back end so that a player's choice survives the
	 * reader being turned off and on, and so that a platform module installing its own back end
	 * inherits them rather than resetting to whatever its voice defaults to.
	 */
	struct FSpeechState
	{
		TUniquePtr<Folio::ISpeechBackend> Backend;
		bool bBackendChosen = false;
		bool bRunning = false;
		int32 Rate = 0;
		int32 Volume = 100;

		TArray<FUtterance> Queue;

		/** Set the moment a line is handed over, because a back end takes a moment to admit it started. */
		bool bSpeaking = false;

		float StaleAfter = 4.0f;

		FTSTicker::FDelegateHandle Tick;
	};

	FSpeechState& SpeechState()
	{
		static FSpeechState Instance;
		return Instance;
	}

	Folio::ISpeechBackend* Backend()
	{
		FSpeechState& Speech = SpeechState();

		// The built-in one is made on first use rather than at startup, so a platform module that
		// installs its own never pays for a voice nothing asked for.
		if (!Speech.bBackendChosen)
		{
			Speech.bBackendChosen = true;
			Speech.Backend = MakeDefaultBackend();
		}

		return Speech.Backend.Get();
	}

	/**
	 * How many lines may wait at once.
	 *
	 * A ceiling rather than a policy: channels and staleness are what a working document relies on,
	 * and this only stops a bug that announces every frame from growing without bound.
	 */
	constexpr int32 MaxQueued = 24;
}

void Folio::FSpeech::SetBackend(TUniquePtr<ISpeechBackend> InBackend)
{
	Stop();

	FSpeechState& Speech = SpeechState();
	Speech.Backend = MoveTemp(InBackend);
	Speech.bBackendChosen = true;
}

bool Folio::FSpeech::IsAvailable()
{
	return Backend() != nullptr;
}

bool Folio::FSpeech::Start()
{
	FSpeechState& Speech = SpeechState();
	if (Speech.bRunning)
	{
		return true;
	}

	ISpeechBackend* Installed = Backend();
	if (!Installed)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("this platform has no speech back end, so the screen reader has nothing to speak with. "
				"The accessibility tree is still built for a platform narrator to read. Install one with "
				"Folio::FSpeech::SetBackend."));

		return false;
	}

	if (!Installed->Start())
	{
		return false;
	}

	Installed->SetRate(Speech.Rate);
	Installed->SetVolume(Speech.Volume);
	Speech.bRunning = true;

	// The core ticker rather than a world timer: a document in its own editor window has no world, and
	// that is exactly where this gets tested.
	Speech.Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		Pump();
		return true;
	}));

	return true;
}

void Folio::FSpeech::Stop()
{
	FSpeechState& Speech = SpeechState();
	if (!Speech.bRunning)
	{
		return;
	}

	if (Speech.Tick.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Speech.Tick);
		Speech.Tick.Reset();
	}

	Speech.Queue.Reset();
	Speech.bSpeaking = false;
	Speech.bRunning = false;

	if (Speech.Backend)
	{
		Speech.Backend->Stop();
	}
}

bool Folio::FSpeech::IsRunning()
{
	return SpeechState().bRunning;
}

void Folio::FSpeech::Say(const FString& Text, const ESpeechPriority Priority, const FName Channel)
{
	FSpeechState& Speech = SpeechState();
	if (!Speech.bRunning || Text.IsEmpty())
	{
		return;
	}

	if (Priority == ESpeechPriority::Assertive)
	{
		// Everything waiting was about a state the player has already left.
		Speech.Queue.Reset();
		Speech.Backend->Speak(Text);
		Speech.bSpeaking = true;
		return;
	}

	FUtterance Line;
	Line.Text = Text;
	Line.Channel = Channel;
	Line.At = FPlatformTime::Seconds();
	Line.bPerishable = Priority == ESpeechPriority::Polite;

	if (!Channel.IsNone())
	{
		if (FUtterance* Existing = Speech.Queue.FindByPredicate(
			[Channel](const FUtterance& Queued) { return Queued.Channel == Channel; }))
		{
			// In place rather than moved to the back: a value that keeps changing should be read where
			// it was going to be read, saying whatever it says by then.
			*Existing = MoveTemp(Line);
			return;
		}
	}

	Speech.Queue.Add(MoveTemp(Line));

	while (Speech.Queue.Num() > MaxQueued)
	{
		Speech.Queue.RemoveAt(0, EAllowShrinking::No);
	}
}

void Folio::FSpeech::Silence()
{
	FSpeechState& Speech = SpeechState();
	if (!Speech.bRunning)
	{
		return;
	}

	Speech.Queue.Reset();
	Speech.bSpeaking = false;
	Speech.Backend->Silence();
}

void Folio::FSpeech::Pump()
{
	FSpeechState& Speech = SpeechState();
	if (!Speech.bRunning)
	{
		return;
	}

	if (Speech.StaleAfter > 0.0f)
	{
		const double Now = FPlatformTime::Seconds();
		const double Limit = double(Speech.StaleAfter);
		Speech.Queue.RemoveAll([Now, Limit](const FUtterance& Line)
		{
			return Line.bPerishable && Now - Line.At > Limit;
		});
	}

	if (Speech.bSpeaking && Speech.Backend->IsSpeaking())
	{
		return;
	}

	Speech.bSpeaking = false;

	if (Speech.Queue.IsEmpty())
	{
		return;
	}

	const FUtterance Line = Speech.Queue[0];
	Speech.Queue.RemoveAt(0, EAllowShrinking::No);

	Speech.Backend->Speak(Line.Text);
	Speech.bSpeaking = true;
}

int32 Folio::FSpeech::NumQueued()
{
	return SpeechState().Queue.Num();
}

void Folio::FSpeech::SetStaleAfter(const float Seconds)
{
	SpeechState().StaleAfter = FMath::Max(0.0f, Seconds);
}

float Folio::FSpeech::GetStaleAfter()
{
	return SpeechState().StaleAfter;
}

void Folio::FSpeech::SetRate(const int32 Rate)
{
	FSpeechState& Speech = SpeechState();
	Speech.Rate = FMath::Clamp(Rate, -10, 10);

	if (Speech.bRunning)
	{
		Speech.Backend->SetRate(Speech.Rate);
	}
}

int32 Folio::FSpeech::GetRate()
{
	return SpeechState().Rate;
}

void Folio::FSpeech::SetVolume(const int32 Volume)
{
	FSpeechState& Speech = SpeechState();
	Speech.Volume = FMath::Clamp(Volume, 0, 100);

	if (Speech.bRunning)
	{
		Speech.Backend->SetVolume(Speech.Volume);
	}
}

int32 Folio::FSpeech::GetVolume()
{
	return SpeechState().Volume;
}

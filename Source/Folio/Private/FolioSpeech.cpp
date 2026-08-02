#include "FolioSpeech.h"

#include "FolioModule.h"

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

		virtual void Say(const FString& Text, const bool bInterrupt) override
		{
			if (!Voice)
			{
				return;
			}

			// Always asynchronous. This is called from the game thread and a sentence takes a second or
			// two.
			const DWORD Flags = SPF_ASYNC | (bInterrupt ? SPF_PURGEBEFORESPEAK : 0);
			Voice->Speak(*Text, Flags, nullptr);
		}

		virtual void Silence() override
		{
			// An empty string with PURGEBEFORESPEAK is SAPI's way of saying stop talking now.
			if (Voice)
			{
				Voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
			}
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
	/**
	 * The installed back end and the settings that outlive it.
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
	return true;
}

void Folio::FSpeech::Stop()
{
	FSpeechState& Speech = SpeechState();
	if (!Speech.bRunning)
	{
		return;
	}

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

void Folio::FSpeech::Say(const FString& Text, const bool bInterrupt)
{
	FSpeechState& Speech = SpeechState();
	if (!Speech.bRunning || Text.IsEmpty())
	{
		return;
	}

	Speech.Backend->Say(Text, bInterrupt);
}

void Folio::FSpeech::Silence()
{
	FSpeechState& Speech = SpeechState();
	if (Speech.bRunning)
	{
		Speech.Backend->Silence();
	}
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

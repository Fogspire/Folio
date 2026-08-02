#include "FolioSpeech.h"

#include "FolioModule.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/COMPointer.h"
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
	struct FSapiVoice
	{
		ISpVoice* Voice = nullptr;
		bool bOwnsCom = false;
		int32 Rate = 0;
		int32 Volume = 100;
	};

	FSapiVoice& Sapi()
	{
		static FSapiVoice Instance;
		return Instance;
	}
}

bool Folio::FSpeech::IsAvailable()
{
	return true;
}

bool Folio::FSpeech::Start()
{
	FSapiVoice& State = Sapi();
	if (State.Voice)
	{
		return true;
	}

	// Unreal has usually initialised COM on this thread already. RPC_E_CHANGED_MODE means it did so
	// with the other threading model, which is not a failure for an in-process object like this one.
	const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	State.bOwnsCom = SUCCEEDED(ComResult);

	ISpVoice* Voice = nullptr;
	const HRESULT Created = CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL,
		__uuidof(ISpVoice), (void**)&Voice);

	if (FAILED(Created) || !Voice)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("no speech voice is available on this machine (0x%08x), so the screen reader cannot "
				"speak. A player running NVDA or Narrator is unaffected: that path reads the "
				"accessibility tree instead."),
			(uint32)Created);

		if (State.bOwnsCom)
		{
			CoUninitialize();
			State.bOwnsCom = false;
		}

		return false;
	}

	State.Voice = Voice;
	State.Voice->SetRate(State.Rate);
	State.Voice->SetVolume((USHORT)FMath::Clamp(State.Volume, 0, 100));

	return true;
}

void Folio::FSpeech::Stop()
{
	FSapiVoice& State = Sapi();
	if (!State.Voice)
	{
		return;
	}

	// An empty string with PURGEBEFORESPEAK is SAPI's way of saying stop talking now, which turning
	// the reader off has to do or the last sentence outlives the setting.
	State.Voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
	State.Voice->Release();
	State.Voice = nullptr;

	if (State.bOwnsCom)
	{
		CoUninitialize();
		State.bOwnsCom = false;
	}
}

bool Folio::FSpeech::IsRunning()
{
	return Sapi().Voice != nullptr;
}

void Folio::FSpeech::Say(const FString& Text, const bool bInterrupt)
{
	FSapiVoice& State = Sapi();
	if (!State.Voice || Text.IsEmpty())
	{
		return;
	}

	// Always asynchronous. This is called from the game thread, and a sentence takes a second or two.
	const DWORD Flags = SPF_ASYNC | (bInterrupt ? SPF_PURGEBEFORESPEAK : 0);
	State.Voice->Speak(*Text, Flags, nullptr);
}

void Folio::FSpeech::SetRate(const int32 Rate)
{
	FSapiVoice& State = Sapi();
	State.Rate = FMath::Clamp(Rate, -10, 10);

	if (State.Voice)
	{
		State.Voice->SetRate(State.Rate);
	}
}

int32 Folio::FSpeech::GetRate()
{
	return Sapi().Rate;
}

void Folio::FSpeech::SetVolume(const int32 Volume)
{
	FSapiVoice& State = Sapi();
	State.Volume = FMath::Clamp(Volume, 0, 100);

	if (State.Voice)
	{
		State.Voice->SetVolume((USHORT)State.Volume);
	}
}

int32 Folio::FSpeech::GetVolume()
{
	return Sapi().Volume;
}

#else

namespace
{
	int32 GRate = 0;
	int32 GVolume = 100;
}

bool Folio::FSpeech::IsAvailable()
{
	return false;
}

bool Folio::FSpeech::Start()
{
	UE_LOG(LogFolio, Warning,
		TEXT("this platform has no speech back end, so the screen reader has nothing to speak with. "
			"The accessibility tree is still built for a platform narrator to read."));

	return false;
}

void Folio::FSpeech::Stop()
{
}

bool Folio::FSpeech::IsRunning()
{
	return false;
}

void Folio::FSpeech::Say(const FString&, bool)
{
}

void Folio::FSpeech::SetRate(const int32 Rate)
{
	GRate = FMath::Clamp(Rate, -10, 10);
}

int32 Folio::FSpeech::GetRate()
{
	return GRate;
}

void Folio::FSpeech::SetVolume(const int32 Volume)
{
	GVolume = FMath::Clamp(Volume, 0, 100);
}

int32 Folio::FSpeech::GetVolume()
{
	return GVolume;
}

#endif

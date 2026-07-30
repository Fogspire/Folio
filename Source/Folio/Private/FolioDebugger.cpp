#include "FolioDebugger.h"

#if !UE_BUILD_SHIPPING

#include "FolioModule.h"
#include "SFolioCanvas.h"

#include <RmlUi/Debugger.h>

namespace Folio::Debug
{
	namespace
	{
		SFolioCanvas* HostCanvas = nullptr;

		/** A canvas outlives nothing here: it is only a host while it is still in the live set. */
		bool IsAlive(const SFolioCanvas* Canvas)
		{
			return Canvas && SFolioCanvas::GetLiveCanvases().Contains(Canvas);
		}

		bool RmlIsUp()
		{
			return FFolioModule::IsAvailable() && FFolioModule::Get().IsRmlInitialised();
		}
	}

	SFolioCanvas* Host()
	{
		if (!IsAlive(HostCanvas))
		{
			HostCanvas = nullptr;
		}
		return HostCanvas;
	}

	bool Attach(SFolioCanvas* Canvas)
	{
		if (!RmlIsUp() || !IsAlive(Canvas) || !Canvas->GetContext())
		{
			return false;
		}

		if (Host() == Canvas)
		{
			return true;
		}

		// Shutdown first even when the old host is already gone: RmlUi only releases the debugger
		// if the host context was destroyed, and re-initialising over a live one leaves two.
		if (Host())
		{
			Rml::Debugger::Shutdown();
		}
		HostCanvas = nullptr;

		if (!Rml::Debugger::Initialise(Canvas->GetContext()))
		{
			UE_LOG(LogFolio, Warning, TEXT("the RmlUi debugger would not initialise"));
			return false;
		}

		HostCanvas = Canvas;
		return true;
	}

	void Detach()
	{
		if (Host() && RmlIsUp())
		{
			Rml::Debugger::Shutdown();
		}
		HostCanvas = nullptr;
	}

	void SetVisible(const bool bVisible)
	{
		if (Host())
		{
			Rml::Debugger::SetVisible(bVisible);
		}
	}

	bool IsVisible()
	{
		return Host() != nullptr && Rml::Debugger::IsVisible();
	}
}

#endif

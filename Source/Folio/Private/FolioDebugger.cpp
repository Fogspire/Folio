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

		bool bTookMouse = false;

		/**
		 * Takes the mouse for the debugger, or gives it back.
		 *
		 * The debugger's own documents live in the canvas's context, so they are only clickable if the
		 * canvas takes the mouse, and a HUD's canvas is hit-test invisible so the game behind it can be
		 * played through. Tied to the debugger being *visible* rather than attached, so firing is dead
		 * only while it is on screen.
		 */
		void TakeMouse(SFolioCanvas* Canvas, const bool bTake)
		{
			if (!Canvas || bTake == bTookMouse)
			{
				return;
			}

			bTookMouse = bTake;

			if (bTake && !Canvas->IsInteractive())
			{
				UE_LOG(LogFolio, Display,
					TEXT("the debugger has taken the mouse from this document, which was not taking "
						"it. Anything behind it - firing, looking - stops until 'folio.Debugger off'."));
			}

			Canvas->SetDebuggerHold(bTake);
		}

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
		if (SFolioCanvas* Canvas = Host())
		{
			TakeMouse(Canvas, false);

			if (RmlIsUp())
			{
				Rml::Debugger::Shutdown();
			}
		}

		HostCanvas = nullptr;
	}

	void SetVisible(const bool bVisible)
	{
		if (SFolioCanvas* Canvas = Host())
		{
			Rml::Debugger::SetVisible(bVisible);
			TakeMouse(Canvas, bVisible);
		}
	}

	bool IsVisible()
	{
		return Host() != nullptr && Rml::Debugger::IsVisible();
	}
}

#endif

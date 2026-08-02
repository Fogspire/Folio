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

		/**
		 * What the host's interactivity was before the debugger took it.
		 *
		 * The debugger's own documents live in the canvas's context, so they are only clickable if the
		 * canvas takes the mouse - and a HUD sets `bInteractive = false`, which makes the widget
		 * hit-test invisible so the game behind it can be played through. Attaching to one gives you a
		 * debugger you can see and cannot use, with every click landing in the game.
		 */
		bool bRestoreInteractive = true;
		bool bTookMouse = false;

		/**
		 * Takes the mouse for the debugger, or gives it back.
		 *
		 * Tied to the debugger being *visible* rather than attached, because taking it is not free: the
		 * canvas is hit-test invisible so that the game behind a HUD can be played through, and holding
		 * that open would leave firing dead until somebody thought to detach.
		 */
		void TakeMouse(SFolioCanvas* Canvas, const bool bTake)
		{
			if (!Canvas || bTake == bTookMouse)
			{
				return;
			}

			if (bTake)
			{
				bRestoreInteractive = Canvas->IsInteractive();
				bTookMouse = true;
				Canvas->SetInteractive(true);

				if (!bRestoreInteractive)
				{
					UE_LOG(LogFolio, Display,
						TEXT("the debugger has taken the mouse from this document, which was not taking "
							"it. Anything behind it - firing, looking - stops until 'folio.Debugger off'."));
				}

				return;
			}

			bTookMouse = false;

			// Put back what the document asked for. A game that changed its mind while the debugger was
			// up loses that change, which is the lesser wrong against leaving a HUD eating every click.
			if (!bRestoreInteractive)
			{
				Canvas->SetInteractive(false);
			}
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
		bRestoreInteractive = true;
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

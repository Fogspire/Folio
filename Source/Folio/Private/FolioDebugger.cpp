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

		bRestoreInteractive = Canvas->IsInteractive();
		Canvas->SetInteractive(true);

		return true;
	}

	void Detach()
	{
		if (SFolioCanvas* Canvas = Host())
		{
			if (RmlIsUp())
			{
				Rml::Debugger::Shutdown();
			}

			// Put back what the document asked for. A game that changed its mind while the debugger was
			// up loses that change, which is the lesser wrong against leaving a HUD eating every click.
			if (!bRestoreInteractive)
			{
				Canvas->SetInteractive(false);
			}
		}

		HostCanvas = nullptr;
		bRestoreInteractive = true;
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

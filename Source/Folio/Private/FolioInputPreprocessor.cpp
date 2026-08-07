#include "FolioInputPreprocessor.h"

#include "FolioInput.h"
#include "FolioModule.h"
#include "SFolioCanvas.h"
#include "SFolioWorldFocus.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TSharedPtr<FFolioInputPreprocessor> Processor;

	TAutoConsoleVariable<int32> CVarInputTrace(TEXT("folio.Input.Trace"), 0,
		TEXT("Logs every gamepad event Slate delivers, and which document took it. For working out "
			"whether a pad reaches Folio at all."),
		ECVF_Default);

	/**
	 * The canvas this event belongs to, or null.
	 *
	 * Normally that is simply the focused widget. The second case exists because CommonUI's action
	 * router reassigns focus to the game viewport whenever it reapplies its input config, so a
	 * document that asked to keep focus can lose it without anything going wrong that a log would
	 * show. Taking it back here, when input actually arrives, settles in one event rather than
	 * fighting the router every frame.
	 */
	SFolioCanvas* CanvasForEvent(FSlateApplication& SlateApp, const FInputEvent& Event,
		const bool bMayReclaim = true)
	{
		const TArray<SFolioCanvas*>& Live = SFolioCanvas::GetLiveCanvases();
		if (Live.Num() == 0)
		{
			return nullptr;
		}

		if (const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(Event.GetUserIndex()))
		{
			// Interactive only. A HUD or an overlay left holding focus is not an input target, and
			// treating it as one is indistinguishable from a document that has stopped responding:
			// every key goes somewhere that does nothing with it and nothing ever reclaims.
			SFolioCanvas* const* Found = Live.FindByPredicate(
				[&Focused](const SFolioCanvas* Canvas)
				{
					return Canvas == Focused.Get() && Canvas->IsInteractive();
				});

			if (Found)
			{
				return *Found;
			}
		}

		SFolioCanvas* const* Claiming = bMayReclaim
			? Live.FindByPredicate([](const SFolioCanvas* Canvas) { return Canvas->WantsFocus(); })
			: nullptr;

		if (!Claiming)
		{
			return nullptr;
		}

		SlateApp.SetUserFocus(Event.GetUserIndex(), (*Claiming)->AsShared(), EFocusCause::SetDirectly);

		if (CVarInputTrace.GetValueOnAnyThread() != 0)
		{
			UE_LOG(LogFolio, Display,
				TEXT("input trace: focus had been taken from the document, reclaimed for user %d"),
				Event.GetUserIndex());
		}

		return *Claiming;
	}

	/**
	 * Whether a screen document is asking to hold input.
	 *
	 * A menu is open in front of the world. Routing the pad to a panel on a wall behind it is never
	 * what the player meant, so this is checked before the world sink gets a look in.
	 */
	bool ScreenClaimsInput()
	{
		const TArray<SFolioCanvas*>& Live = SFolioCanvas::GetLiveCanvases();
		return Live.ContainsByPredicate([](const SFolioCanvas* Canvas) { return Canvas->WantsFocus(); });
	}

	bool Tracing()
	{
		return CVarInputTrace.GetValueOnAnyThread() != 0;
	}

	void Trace(const TCHAR* What, const FInputEvent& Event, const FKey& Key,
		const SFolioCanvas* Canvas, const bool bConsumed)
	{
		if (!Tracing())
		{
			return;
		}

		// Every device, not just gamepads: an absence of gamepad lines only means something when
		// keyboard lines are arriving to prove the trace itself is live.
		UE_LOG(LogFolio, Display, TEXT("input trace: %s %s gamepad=%s user=%d focus=%s consumed=%s"),
			What, *Key.ToString(), Key.IsGamepadKey() ? TEXT("yes") : TEXT("no"),
			Event.GetUserIndex(),
			Canvas ? TEXT("folio") : TEXT("elsewhere"),
			bConsumed ? TEXT("yes") : TEXT("no"));
	}
}

void FFolioInputPreprocessor::Register()
{
	if (Processor.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	Processor = MakeShared<FFolioInputPreprocessor>();

	// PreGame, which is ahead of Game where CommonUI's analog cursor registers by default. Ahead of
	// Engine would also intercept editor input, which is not ours to take.
	FSlateApplication::Get().RegisterInputPreProcessor(Processor, EInputPreProcessorType::PreGame);
}

void FFolioInputPreprocessor::Unregister()
{
	if (Processor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
	}

	Processor.Reset();
}

bool FFolioInputPreprocessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	// The console reads the keyboard through the viewport widget it focused for itself. Reclaiming
	// that on the next key is what leaves it open with nothing able to be typed into it.
	if (Folio::IsConsoleOpen())
	{
		return false;
	}

	// A world document holds focus through its own sink, which is not a canvas. A screen document
	// asking for input comes first: with a menu open, every pad press belongs to the menu and not to
	// a panel on a wall behind it.
	if (SFolioWorldFocus::IsActive() && !ScreenClaimsInput())
	{
		SFolioWorldFocus::ReclaimFocus(SlateApp, InKeyEvent.GetUserIndex());

		// Traced like any other route. Without this the one path that consumes input and returns
		// before every other check is also the one path that says nothing about having done so,
		// which reads as a trace that is not running at all.
		if (Tracing())
		{
			UE_LOG(LogFolio, Display, TEXT("input trace: key down %s taken by a world document"),
				*InKeyEvent.GetKey().ToString());
		}

		if (InKeyEvent.GetKey().IsGamepadKey())
		{
			SFolioWorldFocus::RouteGamepadKeyDown(InKeyEvent);
			return true;
		}

		// Slate reads the focus path after the pre-processors run, so a keyboard event refocused here
		// reaches the sink on its own and must not be consumed.
		return false;
	}

	// Called for every device, because reclaiming focus is what keyboard input needs too. Slate
	// reads the focus path after the pre-processors run, so a keyboard event refocused here is
	// routed to the document by Slate itself and does not need consuming.
	SFolioCanvas* Canvas = CanvasForEvent(SlateApp, InKeyEvent);

	if (!InKeyEvent.GetKey().IsGamepadKey())
	{
		Trace(TEXT("key down"), InKeyEvent, InKeyEvent.GetKey(), Canvas, false);
		return false;
	}

	const bool bConsumed = Canvas && Canvas->RouteGamepadKeyDown(InKeyEvent);
	Trace(TEXT("key down"), InKeyEvent, InKeyEvent.GetKey(), Canvas, bConsumed);

	// Only what the document claimed. Navigation, confirm and back are claimed whether RmlUi could
	// act on them or not, so none of those reach the pawn behind the menu. Anything else - start,
	// select, a shoulder button - is a key the document has no meaning for, and a game is entitled
	// to bind it while a menu is up. Swallowing those is how a start button that should close the
	// menu ends up doing nothing at all.
	return bConsumed;
}

bool FFolioInputPreprocessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (Folio::IsConsoleOpen())
	{
		return false;
	}

	if (!InKeyEvent.GetKey().IsGamepadKey())
	{
		return false;
	}

	if (SFolioWorldFocus::IsActive() && !ScreenClaimsInput())
	{
		// Paired with the press, or RmlUi keeps thinking the button is held.
		SFolioWorldFocus::RouteGamepadKeyUp(InKeyEvent);
		return true;
	}

	SFolioCanvas* Canvas = CanvasForEvent(SlateApp, InKeyEvent);
	if (!Canvas)
	{
		return false;
	}

	// Paired with the press, or RmlUi keeps thinking the button is held. Consumed on the same terms.
	return Canvas->RouteGamepadKeyUp(InKeyEvent);
}

bool FFolioInputPreprocessor::HandleAnalogInputEvent(FSlateApplication& SlateApp,
	const FAnalogInputEvent& InAnalogInputEvent)
{
	if (Folio::IsConsoleOpen())
	{
		return false;
	}

	if (!InAnalogInputEvent.GetKey().IsGamepadKey())
	{
		return false;
	}

	if (SFolioWorldFocus::IsActive() && !ScreenClaimsInput())
	{
		SFolioWorldFocus::RouteGamepadAnalog(InAnalogInputEvent);
		return true;
	}

	// A resting stick reports zero every frame. Reclaiming focus off the back of that would fight
	// the action router continuously, so only a real push counts as intent.
	const bool bPushed = FMath::Abs(InAnalogInputEvent.GetAnalogValue()) >= 0.2f;
	SFolioCanvas* Canvas = CanvasForEvent(SlateApp, InAnalogInputEvent, bPushed);

	if (Tracing() && bPushed)
	{
		Trace(TEXT("analog"), InAnalogInputEvent, InAnalogInputEvent.GetKey(), Canvas, Canvas != nullptr);
	}

	if (!Canvas)
	{
		return false;
	}

	Canvas->RouteGamepadAnalog(InAnalogInputEvent);

	// A stick at rest reports zero every frame. Consuming those too would be correct but noisy;
	// what matters is that a pushed stick does not also walk the character.
	return true;
}

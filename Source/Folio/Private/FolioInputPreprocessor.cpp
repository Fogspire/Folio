#include "FolioInputPreprocessor.h"

#include "FolioModule.h"
#include "SFolioCanvas.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TSharedPtr<FFolioInputPreprocessor> Processor;

	TAutoConsoleVariable<int32> CVarInputTrace(TEXT("folio.Input.Trace"), 0,
		TEXT("Logs every gamepad event Slate delivers, and which document took it. For working out "
			"whether a pad reaches Folio at all."),
		ECVF_Default);

	/** The focused canvas for this event's user, or null when focus is anywhere else. */
	SFolioCanvas* FocusedCanvas(FSlateApplication& SlateApp, const FInputEvent& Event)
	{
		const TArray<SFolioCanvas*>& Live = SFolioCanvas::GetLiveCanvases();
		if (Live.Num() == 0)
		{
			return nullptr;
		}

		const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(Event.GetUserIndex());
		if (!Focused.IsValid())
		{
			return nullptr;
		}

		SFolioCanvas* const* Found = Live.FindByPredicate(
			[&Focused](const SFolioCanvas* Canvas) { return Canvas == Focused.Get(); });

		return Found ? *Found : nullptr;
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
	if (!InKeyEvent.GetKey().IsGamepadKey())
	{
		// Traced but never touched. Keyboard routing is Slate's business.
		Trace(TEXT("key down"), InKeyEvent, InKeyEvent.GetKey(),
			FocusedCanvas(SlateApp, InKeyEvent), false);
		return false;
	}

	SFolioCanvas* Canvas = FocusedCanvas(SlateApp, InKeyEvent);
	const bool bConsumed = Canvas && Canvas->RouteGamepadKeyDown(InKeyEvent);
	Trace(TEXT("key down"), InKeyEvent, InKeyEvent.GetKey(), Canvas, bConsumed);

	// Consumed whenever a document was focused, handled or not. A pad press that a document chose
	// to ignore must not fall through and drive the player character behind it.
	return Canvas != nullptr;
}

bool FFolioInputPreprocessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (!InKeyEvent.GetKey().IsGamepadKey())
	{
		return false;
	}

	SFolioCanvas* Canvas = FocusedCanvas(SlateApp, InKeyEvent);
	if (Canvas)
	{
		// Paired with the press, or RmlUi keeps thinking the button is held.
		Canvas->RouteGamepadKeyUp(InKeyEvent);
	}

	return Canvas != nullptr;
}

bool FFolioInputPreprocessor::HandleAnalogInputEvent(FSlateApplication& SlateApp,
	const FAnalogInputEvent& InAnalogInputEvent)
{
	if (!InAnalogInputEvent.GetKey().IsGamepadKey())
	{
		return false;
	}

	SFolioCanvas* Canvas = FocusedCanvas(SlateApp, InAnalogInputEvent);

	// Only past the deadzone, or a resting stick fills the log at sixty lines a second.
	if (Tracing() && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) >= 0.2f)
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

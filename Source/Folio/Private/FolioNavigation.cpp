#include "FolioNavigation.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarNavArrows(TEXT("folio.Nav.Arrows"), 1,
		TEXT("Arrow keys navigate a document. Off leaves them to text fields alone."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarNavWASD(TEXT("folio.Nav.WASD"), 1,
		TEXT("W, A, S and D navigate a document while nothing is taking text."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarNavIJKL(TEXT("folio.Nav.IJKL"), 1,
		TEXT("I, J, K and L navigate a document while nothing is taking text."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarNavGamepad(TEXT("folio.Nav.Gamepad"), 1,
		TEXT("The gamepad navigates a document: d-pad, sticks, face buttons and shoulders."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarSwapConfirmBack(TEXT("folio.Nav.SwapConfirmBack"), 0,
		TEXT("The east face button confirms and the south one goes back, rather than the reverse."),
		ECVF_Default);

	FSimpleMulticastDelegate ConfirmButtonChanged;

	/// Registered once, so the console and a settings screen both reach the same notification.
	void EnsureConfirmCallback()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}

		bRegistered = true;
		CVarSwapConfirmBack->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
			{
				ConfirmButtonChanged.Broadcast();
			}));
	}
}

namespace Folio
{
	const TCHAR* DescribeDevice(const EInputDevice Device)
	{
		switch (Device)
		{
		case EInputDevice::Keyboard: return TEXT("keyboard");
		case EInputDevice::Mouse:    return TEXT("mouse");
		case EInputDevice::Gamepad:  return TEXT("gamepad");
		default:                     return TEXT("unknown");
		}
	}

	EInputDevice DeviceForKey(const FKey& Key)
	{
		if (Key.IsGamepadKey())
		{
			return EInputDevice::Gamepad;
		}
		if (Key.IsMouseButton())
		{
			return EInputDevice::Mouse;
		}
		return EInputDevice::Keyboard;
	}

	bool ArrowsNavigate()
	{
		return CVarNavArrows.GetValueOnAnyThread() != 0;
	}

	EConfirmButton ConfirmButton()
	{
		return CVarSwapConfirmBack.GetValueOnAnyThread() != 0
			? EConfirmButton::East
			: EConfirmButton::South;
	}

	void SetConfirmButton(const EConfirmButton Button)
	{
		EnsureConfirmCallback();
		CVarSwapConfirmBack->Set(Button == EConfirmButton::East ? 1 : 0, ECVF_SetByGameSetting);
	}

	FSimpleMulticastDelegate& OnConfirmButtonChanged()
	{
		EnsureConfirmCallback();
		return ConfirmButtonChanged;
	}

	Rml::Input::KeyIdentifier ToNavigationKey(const FKey& Key)
	{
		using namespace Rml::Input;

		const bool bGamepad = CVarNavGamepad.GetValueOnAnyThread() != 0;
		const bool bWASD = CVarNavWASD.GetValueOnAnyThread() != 0;
		const bool bIJKL = CVarNavIJKL.GetValueOnAnyThread() != 0;

		if (Key.IsGamepadKey() && !bGamepad)
		{
			return KI_UNKNOWN;
		}

		// The gamepad. Both sticks' digital equivalents come through as d-pad keys, so a stick
		// pushed to its extreme navigates even before FAnalogNavigator sees it.
		if (Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up
			|| Key == EKeys::Gamepad_RightStick_Up)
		{
			return KI_UP;
		}
		if (Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down
			|| Key == EKeys::Gamepad_RightStick_Down)
		{
			return KI_DOWN;
		}
		if (Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left
			|| Key == EKeys::Gamepad_RightStick_Left)
		{
			return KI_LEFT;
		}
		if (Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right
			|| Key == EKeys::Gamepad_RightStick_Right)
		{
			return KI_RIGHT;
		}

		// Confirm and back. RmlUi turns Return on a focused element into a click, and Escape is
		// what a document listens for to close itself.
		const bool bEastConfirms = ConfirmButton() == EConfirmButton::East;
		if (Key == EKeys::Gamepad_FaceButton_Bottom)
		{
			return bEastConfirms ? KI_ESCAPE : KI_RETURN;
		}
		if (Key == EKeys::Gamepad_FaceButton_Right)
		{
			return bEastConfirms ? KI_RETURN : KI_ESCAPE;
		}

		// Shoulders page through a tab bar, which is what Tab does in a document.
		if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder)
		{
			return KI_TAB;
		}

		// WASD and IJKL, live at the same time as the arrows.
		//
		// Not folded into ToRmlKey: those letters still have to reach a text field as letters.
		// The canvas asks for a navigation meaning only when nothing is being typed into.
		if (bWASD)
		{
			if (Key == EKeys::W) { return KI_UP; }
			if (Key == EKeys::S) { return KI_DOWN; }
			if (Key == EKeys::A) { return KI_LEFT; }
			if (Key == EKeys::D) { return KI_RIGHT; }
		}

		if (bIJKL)
		{
			if (Key == EKeys::I) { return KI_UP; }
			if (Key == EKeys::K) { return KI_DOWN; }
			if (Key == EKeys::J) { return KI_LEFT; }
			if (Key == EKeys::L) { return KI_RIGHT; }
		}

		return KI_UNKNOWN;
	}

	void FAnalogNavigator::SetAxis(const FKey& Key, const float Value)
	{
		if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_RightX)
		{
			X = Value;
		}
		else if (Key == EKeys::Gamepad_LeftY || Key == EKeys::Gamepad_RightY)
		{
			Y = Value;
		}
	}

	void FAnalogNavigator::Reset()
	{
		X = 0.0f;
		Y = 0.0f;
		Held = Rml::Input::KI_UNKNOWN;
		Countdown = 0.0f;
	}

	Rml::Input::KeyIdentifier FAnalogNavigator::Advance(const float DeltaSeconds)
	{
		using namespace Rml::Input;

		if (CVarNavGamepad.GetValueOnAnyThread() == 0)
		{
			return KI_UNKNOWN;
		}

		// The larger axis wins, so a diagonal push means one direction rather than two. Unreal's
		// Y is positive upwards where RmlUi's screen is positive downwards.
		KeyIdentifier Direction = KI_UNKNOWN;
		if (FMath::Abs(X) >= Deadzone || FMath::Abs(Y) >= Deadzone)
		{
			Direction = FMath::Abs(X) > FMath::Abs(Y)
				? (X > 0.0f ? KI_RIGHT : KI_LEFT)
				: (Y > 0.0f ? KI_UP : KI_DOWN);
		}

		if (Direction == KI_UNKNOWN)
		{
			Held = KI_UNKNOWN;
			Countdown = 0.0f;
			return KI_UNKNOWN;
		}

		// A new direction fires at once. Changing direction without returning to centre counts as
		// new, which is what it feels like to the player.
		if (Direction != Held)
		{
			Held = Direction;
			Countdown = InitialDelay;
			return Direction;
		}

		Countdown -= DeltaSeconds;
		if (Countdown > 0.0f)
		{
			return KI_UNKNOWN;
		}

		Countdown = RepeatInterval;
		return Direction;
	}
}

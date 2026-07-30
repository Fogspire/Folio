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

	/** The built-in schemes, which a binding overrides and folio.Nav.* can switch off. */
	EFolioNavAction DefaultActionForKey(const FKey& Key)
	{
		const bool bGamepad = CVarNavGamepad.GetValueOnAnyThread() != 0;

		if (Key.IsGamepadKey())
		{
			if (!bGamepad)
			{
				return EFolioNavAction::None;
			}

			// Both sticks' digital equivalents arrive as d-pad keys, so a stick pushed to its
			// extreme navigates even before FAnalogNavigator sees it.
			if (Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up
				|| Key == EKeys::Gamepad_RightStick_Up)
			{
				return EFolioNavAction::Up;
			}
			if (Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down
				|| Key == EKeys::Gamepad_RightStick_Down)
			{
				return EFolioNavAction::Down;
			}
			if (Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left
				|| Key == EKeys::Gamepad_RightStick_Left)
			{
				return EFolioNavAction::Left;
			}
			if (Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right
				|| Key == EKeys::Gamepad_RightStick_Right)
			{
				return EFolioNavAction::Right;
			}

			// The swap belongs here and not to an explicit binding: a table naming Confirm has
			// already said which button it means.
			const bool bEastConfirms = ConfirmButton() == EConfirmButton::East;
			if (Key == EKeys::Gamepad_FaceButton_Bottom)
			{
				return bEastConfirms ? EFolioNavAction::Back : EFolioNavAction::Confirm;
			}
			if (Key == EKeys::Gamepad_FaceButton_Right)
			{
				return bEastConfirms ? EFolioNavAction::Confirm : EFolioNavAction::Back;
			}

			// Shoulders page through a tab bar.
			if (Key == EKeys::Gamepad_LeftShoulder)  { return EFolioNavAction::PreviousTab; }
			if (Key == EKeys::Gamepad_RightShoulder) { return EFolioNavAction::NextTab; }

			return EFolioNavAction::None;
		}

		// WASD and IJKL, live at the same time as the arrows.
		//
		// Not folded into ToRmlKey: those letters still have to reach a text field as letters. The
		// canvas asks for a navigation meaning only when nothing is being typed into.
		if (CVarNavWASD.GetValueOnAnyThread() != 0)
		{
			if (Key == EKeys::W) { return EFolioNavAction::Up; }
			if (Key == EKeys::S) { return EFolioNavAction::Down; }
			if (Key == EKeys::A) { return EFolioNavAction::Left; }
			if (Key == EKeys::D) { return EFolioNavAction::Right; }
		}

		if (CVarNavIJKL.GetValueOnAnyThread() != 0)
		{
			if (Key == EKeys::I) { return EFolioNavAction::Up; }
			if (Key == EKeys::K) { return EFolioNavAction::Down; }
			if (Key == EKeys::J) { return EFolioNavAction::Left; }
			if (Key == EKeys::L) { return EFolioNavAction::Right; }
		}

		return EFolioNavAction::None;
	}

	EFolioNavAction ActionForKey(const FKey& Key)
	{
		// A binding wins, and is not subject to the scheme switches: a project that bound a key
		// meant it.
		if (const EFolioNavAction Bound = FindBoundAction(Key); Bound != EFolioNavAction::None)
		{
			return Bound;
		}

		return DefaultsReplaced() ? EFolioNavAction::None : DefaultActionForKey(Key);
	}

	TArray<FKey> DefaultKeysForAction(const EFolioNavAction Action)
	{
		// Derived from DefaultActionForKey rather than listed again, so the two cannot disagree
		// about what a scheme switch does.
		static const TArray<FKey> Candidates = {
			EKeys::W, EKeys::A, EKeys::S, EKeys::D,
			EKeys::I, EKeys::J, EKeys::K, EKeys::L,
			EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down,
			EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
			EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right,
			EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder,
		};

		TArray<FKey> Keys;
		for (const FKey& Key : Candidates)
		{
			if (DefaultActionForKey(Key) == Action)
			{
				Keys.Add(Key);
			}
		}

		// The arrows reach RmlUi through ToRmlKey rather than through an action, so they are not in
		// the candidate list, but a settings screen still has to show them.
		if (ArrowsNavigate())
		{
			switch (Action)
			{
			case EFolioNavAction::Up:    Keys.Add(EKeys::Up); break;
			case EFolioNavAction::Down:  Keys.Add(EKeys::Down); break;
			case EFolioNavAction::Left:  Keys.Add(EKeys::Left); break;
			case EFolioNavAction::Right: Keys.Add(EKeys::Right); break;
			default: break;
			}
		}

		return Keys;
	}

	Rml::Input::KeyIdentifier ToNavigationKey(const FKey& Key)
	{
		int32 Ignored = 0;
		return ToNavigationKey(Key, Ignored);
	}

	Rml::Input::KeyIdentifier ToNavigationKey(const FKey& Key, int32& OutExtraModifiers)
	{
		using namespace Rml::Input;

		const EFolioNavAction Action = ActionForKey(Key);
		if (Action == EFolioNavAction::PreviousTab)
		{
			// Reverse tabbing is shift and Tab. RmlUi reads the direction off the modifier.
			OutExtraModifiers |= KM_SHIFT;
		}

		switch (Action)
		{
		case EFolioNavAction::Up:          return KI_UP;
		case EFolioNavAction::Down:        return KI_DOWN;
		case EFolioNavAction::Left:        return KI_LEFT;
		case EFolioNavAction::Right:       return KI_RIGHT;
		case EFolioNavAction::Confirm:     return KI_RETURN;
		case EFolioNavAction::Back:        return KI_ESCAPE;
		case EFolioNavAction::NextTab:     return KI_TAB;
		case EFolioNavAction::PreviousTab: return KI_TAB;
		default:                           return KI_UNKNOWN;
		}
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

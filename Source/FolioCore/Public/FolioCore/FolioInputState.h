#pragma once

#include <RmlUi/Core/Element.h>

/**
 * What the player is holding, and which gamepad button says yes.
 *
 * Only the vocabulary lives here - the class names, the model field names and the mapping between
 * them. The state itself is owned per host: the Unreal module tracks it from real input, the
 * preview tool sets it from a command line flag. Sharing the names is what keeps a document tested
 * in the preview behaving the same way in game; sharing the state would mean a singleton spanning
 * two DLLs that each link their own copy of RmlUi, which is the bug the font effect instancers
 * already exist to avoid.
 */
namespace Folio
{
	enum class EInputDevice : unsigned char
	{
		Unknown,
		Keyboard,
		Mouse,
		Gamepad,
	};

	/**
	 * Which gamepad face button confirms.
	 *
	 * South is the bottom button - A on Xbox, Cross on PlayStation. East is the right one - B, or
	 * Circle. Nintendo hardware and Japanese PlayStation conventions put confirm on East, so this
	 * is a setting a game exposes to players and may also default from the platform.
	 */
	enum class EConfirmButton : unsigned char
	{
		South,
		East,
	};

	/** The value the reserved 'folio' data model reports for 'device'. */
	inline const char* DeviceName(const EInputDevice Device)
	{
		switch (Device)
		{
		case EInputDevice::Keyboard: return "keyboard";
		case EInputDevice::Mouse:    return "mouse";
		case EInputDevice::Gamepad:  return "gamepad";
		default:                     return "unknown";
		}
	}

	/** The value the reserved 'folio' data model reports for 'confirm'. */
	inline const char* ConfirmName(const EConfirmButton Button)
	{
		return Button == EConfirmButton::East ? "east" : "south";
	}

	/** The value it reports for 'back', which is always the other one. */
	inline const char* BackName(const EConfirmButton Button)
	{
		return Button == EConfirmButton::East ? "south" : "east";
	}

	/**
	 * The classes set on a document body.
	 *
	 * Three groups, exactly one class from each set at a time:
	 *
	 *   folio-device-keyboard | folio-device-mouse | folio-device-gamepad
	 *   folio-input-kbm       | folio-input-gamepad
	 *   folio-confirm-south   | folio-confirm-east
	 *
	 * The second group is the redundant one and it earns its place: swapping button glyphs asks
	 * "gamepad or not", and keyboard and mouse are both not, so without it every rule needs either
	 * a duplicate selector or :not(), and one of those is a maintenance cost and the other is a bet
	 * on selector support.
	 */
	inline const char* DeviceClass(const EInputDevice Device)
	{
		switch (Device)
		{
		case EInputDevice::Keyboard: return "folio-device-keyboard";
		case EInputDevice::Mouse:    return "folio-device-mouse";
		case EInputDevice::Gamepad:  return "folio-device-gamepad";
		default:                     return "folio-device-unknown";
		}
	}

	inline const char* InputKindClass(const EInputDevice Device)
	{
		return Device == EInputDevice::Gamepad ? "folio-input-gamepad" : "folio-input-kbm";
	}

	inline const char* ConfirmClass(const EConfirmButton Button)
	{
		return Button == EConfirmButton::East ? "folio-confirm-east" : "folio-confirm-south";
	}

	/** Field names on the reserved model, so both hosts bind the same spellings. */
	namespace InputModel
	{
		inline const char* Name() { return "folio"; }
		inline const char* Device() { return "device"; }
		inline const char* Gamepad() { return "gamepad"; }
		inline const char* Confirm() { return "confirm"; }
		inline const char* Back() { return "back"; }
	}

	/** Rewrites all three groups on an element, clearing whatever was set before. */
	inline void ApplyInputClasses(Rml::Element* Element, const EInputDevice Device,
		const EConfirmButton Button)
	{
		if (!Element)
		{
			return;
		}

		static const EInputDevice AllDevices[] = { EInputDevice::Unknown, EInputDevice::Keyboard,
			EInputDevice::Mouse, EInputDevice::Gamepad };

		for (const EInputDevice Each : AllDevices)
		{
			Element->SetClass(DeviceClass(Each), Each == Device);
		}

		Element->SetClass(InputKindClass(EInputDevice::Gamepad), Device == EInputDevice::Gamepad);
		Element->SetClass(InputKindClass(EInputDevice::Keyboard), Device != EInputDevice::Gamepad);

		Element->SetClass(ConfirmClass(EConfirmButton::South), Button == EConfirmButton::South);
		Element->SetClass(ConfirmClass(EConfirmButton::East), Button == EConfirmButton::East);
	}
}

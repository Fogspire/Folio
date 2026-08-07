#include "FolioInput.h"

#include "InputCoreTypes.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"

namespace Folio
{
	int32 ToRmlModifiers(const FModifierKeysState& Modifiers)
	{
		int32 Result = 0;
		if (Modifiers.IsControlDown()) { Result |= Rml::Input::KM_CTRL; }
		if (Modifiers.IsShiftDown())   { Result |= Rml::Input::KM_SHIFT; }
		if (Modifiers.IsAltDown())     { Result |= Rml::Input::KM_ALT; }
		if (Modifiers.AreCapsLocked()) { Result |= Rml::Input::KM_CAPSLOCK; }
		return Result;
	}

	int32 ToRmlMouseButton(const FKey& Key)
	{
		if (Key == EKeys::LeftMouseButton)   { return 0; }
		if (Key == EKeys::RightMouseButton)  { return 1; }
		if (Key == EKeys::MiddleMouseButton) { return 2; }
		return -1;
	}

	Rml::Input::KeyIdentifier ToRmlKey(const FKey& Key)
	{
		using namespace Rml::Input;

		// Navigation. Everything directional funnels through these four, whatever produced it.
		if (Key == EKeys::Up)    { return KI_UP; }
		if (Key == EKeys::Down)  { return KI_DOWN; }
		if (Key == EKeys::Left)  { return KI_LEFT; }
		if (Key == EKeys::Right) { return KI_RIGHT; }

		// Activation. RmlUi turns Return and Space on a focused element into a click.
		if (Key == EKeys::Enter)       { return KI_RETURN; }
		if (Key == EKeys::SpaceBar)    { return KI_SPACE; }
		if (Key == EKeys::Escape)      { return KI_ESCAPE; }
		if (Key == EKeys::Tab)         { return KI_TAB; }

		// Text editing.
		if (Key == EKeys::BackSpace)   { return KI_BACK; }
		if (Key == EKeys::Delete)      { return KI_DELETE; }
		if (Key == EKeys::Home)        { return KI_HOME; }
		if (Key == EKeys::End)         { return KI_END; }
		if (Key == EKeys::PageUp)      { return KI_PRIOR; }
		if (Key == EKeys::PageDown)    { return KI_NEXT; }
		if (Key == EKeys::Insert)      { return KI_INSERT; }

		return KI_UNKNOWN;
	}

	bool IsConsoleOpen()
	{
		const UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
		const UConsole* Console = Viewport ? Viewport->ViewportConsole : nullptr;
		return Console && Console->ConsoleActive();
	}
}

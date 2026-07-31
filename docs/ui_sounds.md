# UI sounds

Folio reports four moments and plays nothing.

```cpp
void UMyAudioSubsystem::Initialize(FSubsystemCollectionBase&)
{
	FFolioModule::Get().OnUiSound().AddUObject(this, &UMyAudioSubsystem::PlayUiSound);
}

void UMyAudioSubsystem::PlayUiSound(const Folio::EUiSound Sound)
{
	switch (Sound)
	{
	case Folio::EUiSound::Focus:   Play(FocusCue);   break;
	case Folio::EUiSound::Confirm: Play(ConfirmCue); break;
	case Folio::EUiSound::Back:    Play(BackCue);    break;
	case Folio::EUiSound::Invalid: Play(InvalidCue); break;
	}
}
```

| | |
|---|---|
| `Focus` | Focus moved to a different element. |
| `Confirm` | A focused element was activated — Enter, Space, or the gamepad confirm button. |
| `Back` | Back was pressed, whether from Escape or the pad. |
| `Invalid` | An activation that went nowhere, because the element carries `.is-disabled`. |

## Why it is a delegate rather than an asset

Folio owns no sound assets, and it should not: what a menu sounds like is a game decision, and a UI
library that shipped a click would be a library everyone has to override.

The delegate exists anyway because threading sound through a component library *after the fact* means
touching every component. Adding it now costs a broadcast; adding it later costs the library.

## `Invalid` is the one worth taking seriously

The other three are conventional. `Invalid` is a click on something disabled — a locked option, a
button that is not available yet — and it is the case where a *different* noise does real work.
Without it a player cannot tell "I missed" from "that is not allowed", and the UI reads as unreliable
rather than as strict.

It fires for anything carrying `.is-disabled`, which is the class the component library uses. See
**[input and navigation](input_and_navigation.md)** for the full disabled recipe — a class for the
look, `focus: none` to leave navigation, `pointer-events: none` to stop responding.

## What it does not cover

**Anything specific to a screen.** A weapon-select sound, a purchase confirmation, a page turn — those
belong to the screen, not to the navigation. Listen for the element's own events instead:

```cpp
Element->AddEventListener("click", Listener);
```

or bind them through a data model. See **[calling into game code](hosting.md)**.

**Focus on load.** A document that opens with `autofocus` moves focus, and that will fire `Focus`. If
a menu opening should be silent, gate the first broadcast after a screen change in your handler —
Folio does not know a screen change happened.

## Where to go next

- **[Input and navigation](input_and_navigation.md)** — what produces each of the four moments.
- **[Calling into game code](hosting.md)** — per-element events, for anything a screen owns.

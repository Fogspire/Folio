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
| `Confirm` | A focused element was activated: Enter, Space, or the gamepad confirm button. |
| `Back` | Back was pressed, whether from Escape or the pad. |
| `Invalid` | An activation that went nowhere, because the element carries `.is-disabled`. |

A revealing `<text>` reports through its own delegate, because the document names the sound:

```cpp
void UMyAudioSubsystem::Initialize(FSubsystemCollectionBase&)
{
	FFolioModule::Get().OnTextRevealSound().AddUObject(this, &UMyAudioSubsystem::PlayNamedSound);
}
```

It fires at most once a frame however fast the text types. See [rich text](rich_text.md).

## Why it is a delegate rather than an asset

Folio owns no sound assets: what a menu sounds like is a game decision, and a shipped click is one everyone would have to override. The delegate exists because threading sound through a component library after the fact means touching every component.

## `Invalid` is the one worth taking seriously

`Invalid` is a click on something disabled (a locked option, a button not available yet). Without a distinct noise a player cannot tell "I missed" from "that is not allowed", and the UI reads as unreliable.

It fires for anything carrying `.is-disabled`, the component library's class. See **[input and navigation](input_and_navigation.md)** for the full disabled recipe: a class for the look, `focus: none` to leave navigation, `pointer-events: none` to stop responding.

## What it does not cover

**Anything specific to a screen.** A weapon-select sound, a purchase confirmation, a page turn: those belong to the screen. Listen for the element's own events instead:

```cpp
Element->AddEventListener("click", Listener);
```

or bind them through a data model. See **[calling into game code](hosting.md)**.

**Focus on load.** A document that opens with `autofocus` moves focus, which fires `Focus`. If a menu opening should be silent, gate the first broadcast after a screen change in your handler: Folio does not know a screen change happened.

## Where to go next

- **[Input and navigation](input_and_navigation.md)**: what produces each of the four moments.
- **[Calling into game code](hosting.md)**: per-element events, for anything a screen owns.

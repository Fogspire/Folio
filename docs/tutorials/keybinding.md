# A controls screen

Every action the player can rebind, the keys currently on it, and a press-any-key panel that catches whatever they hit next - including keys the game itself has bound. Written as markup against a model Folio ships, so the screen is a list and four commands.

The finished thing is `rcss/recipes/bindings.rml`. Copy it and change how it looks; what follows is why it is shaped that way.

---

## 1. Where the bindings come from

**Enhanced Input's own user settings, and nowhere else.** `UEnhancedInputUserSettings` holds the player's key profile, saves it, and is what every other system already reads. A UI that kept its own table would be a second source of truth that has to be reconciled on load, on reset, and after every patch that adds an action.

So `UFolioInputBindings` is a view of that profile rather than a copy of it:

```cpp
Bindings = NewObject<UFolioInputBindings>(this);
Bindings->Initialise(GetOwningLocalPlayer());

Document->BindViewModel(TEXT("bindings"), Bindings);
Document->LoadDocument(TEXT("UI/screens/controls.rml"));
```

`Bindings` is now an array of rows, each with the mapping name, the action's display name and category, the primary and secondary keys as text, and whether the player has customised it.

**An action only appears if it is player mappable.** That is a checkbox on the Input Action asset, or an override in the mapping context. A profile with nothing mappable produces an empty screen and a warning in the log saying so, which is the failure people otherwise spend an afternoon on.

## 2. The list

```html
<div class="bind" data-for="row : Bindings">
    <span class="action">{{ row.Action }}</span>
    <span class="cat">{{ row.Category }}</span>

    <span class="pg pg-key focusable" data-if="row.PrimaryKey != ''"
        data-event-click="Rebind(row.Mapping, row.PrimarySlot)">{{ row.PrimaryKey }}</span>
    <span class="pg pg-unbound focusable" data-if="row.PrimaryKey == ''"
        data-event-click="Rebind(row.Mapping, row.PrimarySlot)">&#8212;</span>

    <span class="reset focusable" data-if="row.bCustomised"
        data-event-click="ResetBinding(row.Mapping)">reset</span>
</div>
```

Three things worth copying:

**The keycap is the library's `pg-key`**, the same component a prompt uses. A rebind row and an in-game prompt showing the same key should look identical, and they will not if the screen draws its own box. See **[input prompts](../input_prompts.md)**.

**An unbound slot is still a control.** `pg-unbound` draws an empty cap rather than nothing, because a row with a blank space in it gives the player nowhere to click to *add* a binding.

**`reset` appears only on a customised row.** `bCustomised` comes from Enhanced Input, so the screen never offers to reset something that is already default.

## 3. Catching the key

`Rebind(mapping, slot)` starts listening. Pass the row's `PrimarySlot` or `SecondarySlot`, not 0 and 1: a row holds the keyboard and the pad keys together, so the column is not the engine slot, and writing to the wrong one replaces the other device's key.

While it waits, `bListening` is true and `Listening` names the action, which is everything a panel needs:

```html
<div class="over" data-class-on="bListening">
    <div class="big">PRESS A KEY FOR {{ Listening }}</div>
    <div class="small">Escape cancels</div>
</div>
```

**The capture is an input pre-processor, not a focused widget.** That is the whole reason it works: the point is to catch keys nothing has asked for. A gamepad face button, a mouse thumb button, a key the game has bound to crouch - a focused widget sees none of those, or sees them after the game has already acted on them. A pre-processor runs before any of it and takes the event.

Escape or the pad's view button cancels rather than binding, because a player who has changed their mind needs one key that is never a binding. A key from the other device is swallowed and ignored, so a keyboard page never takes a pad button.

## 4. When the key is already taken

Silently stealing a key from another action is the behaviour players complain about, so `ConfirmConflict` exists:

```html
<div class="over" data-class-on="bConflict">
    <div class="big">{{ ConflictKey }} IS ALREADY {{ ConflictWith }}</div>
    <span class="reset focusable" data-event-click="ConfirmConflict">rebind anyway</span>
    <span class="reset focusable" data-event-click="CancelRebind">cancel</span>
</div>
```

`ConflictWith` is the *display names* of the actions already on that key, not their mapping names. "Already bound" without saying to what is a warning the player cannot act on.

## 5. The rest of the commands

| | |
|---|---|
| `Rebind(mapping, slot)` | Listen, then apply. |
| `CancelRebind()` | Stop listening, or dismiss the conflict warning. |
| `ConfirmConflict()` | Apply the key that was already taken. |
| `ClearBinding(mapping)` | This device's keys to nothing. |
| `ResetBinding(mapping)` | This device's keys back to the action's default. |
| `ResetAll()` | Every action, on this device. |

Every one of them applies and saves through Enhanced Input, then re-reads the profile, so the list reflects what was actually stored rather than what was asked for. A mapping the engine refuses - and it can refuse - shows up as the row not changing, with the reason in the log.

## 6. What to check before shipping it

**Rebind something to a key the game uses.** The capture takes the event, so the game must not also act on it while the panel is up. If your pawn moves while you are choosing a key, something is registered ahead of Folio's pre-processor.

**Rebind with a gamepad.** The face buttons have to reach the panel, and on a project with CommonUI that is where an input config can quietly take them first. See **[hosting a document](../hosting.md)**.

**Clear a binding, then look at the game's prompts.** A prompt for an unbound action should say so rather than showing a stale key - `bBound` on a prompt is false and the row can hide itself.

**Reset to default, and restart.** Reset writes through to the saved profile; if a restart brings the old binding back, the save did not happen and nothing in the UI would have told you.

## Where to go next

- **[Input prompts](../input_prompts.md)**: showing those bindings everywhere else in the game.
- **[Hosting a document](../hosting.md)**: focus, and what CommonUI does to it.
- **[Data binding](../data_binding.md)**: the model side, and why the commands take strings.

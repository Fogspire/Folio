# Hosting a document

`folio.Show` is for looking at something. Shipping a screen means putting a document inside whatever
widget tree the game already has, and deciding who has focus. That is `UFolioWidget`.

Folio replaces UMG for *building* UI. This class exists for the boundary, not for building anything:
a game keeps its HUD layout, its CommonUI stack, its menu flow, and puts a document inside it. There
is no step where the shell has to be ported before the first screen can exist.

## Putting one in a widget tree

In the UMG designer the palette has **Folio Document** under Folio. Drop it in, set **Document Path**
to the same kind of path `folio.Show` takes — `ui/screens/pause.rml`, relative to the project
directory, or `/folio/...` to reach inside the plugin.

**Desired Size** is what the widget reports to Slate layout, not a limit on the document. A document
is laid out to whatever size it ends up being given, so in a fill slot this value never comes up; it
matters in a canvas panel or an auto-sized box.

From C++ or Blueprint, `Load Document` swaps what is up without rebuilding the widget.

## Showing game values

`Bind View Model (Name, Source, Poll Every Frame)` binds any `UObject`'s properties as a named data
model. The document reads it the way it reads any model:

```html
<body data-model="player">
	<div class="bar" data-style-width="format(health / maxHealth * 100, 0) + '%'"></div>
</body>
```

```cpp
Document->BindViewModel(TEXT("player"), PlayerViewModel);
```

A source implementing `INotifyFieldValueChanged` — which `UMVVMViewModelBase` does — pushes changes as
its fields change. One that does not is re-read once a frame instead, which costs work per bound
field, so a view model is the better source when there is a choice.

**Bind before the document loads.** RmlUi resolves a document's bindings as it loads and offers no way
to attach a model afterwards. `BindViewModel` is callable at any point, including before the widget is
built — the widget defers loading until its bindings are in, and rebuilds the document if one is
already up — so a Blueprint construction script is the natural place. What does not work is binding
after the screen is visible and expecting `{{ braces }}` to start resolving.

[Data binding](data_binding.md) covers the document side.

## Focus

This is the part that goes wrong silently, so it is worth understanding rather than copying.

A document only receives navigation and text while its canvas holds Slate focus. `Take Focus` moves
focus to it. In a project with no CommonUI, that is the whole story.

### With CommonUI

CommonUI's action router asks its leaf-most activatable widget for a `GetDesiredFocusTarget()`, and
when it gets none it can use, it focuses **the game viewport instead** — silently taking focus off a
document that had it. Worse, it reapplies its input config on its own schedule, so a document can hold
focus perfectly and then lose it later with nothing in the log to say why.

The symptom is a gamepad doing nothing at all while the same screen works fine from the keyboard.
Nothing about that points at CommonUI.

Return the widget from the override and it is solved:

```cpp
UWidget* UMyScreen::GetDesiredFocusTarget() const override
{
	return Document;
}
```

That works because `UWidget::SupportsKeyboardFocus` defers to the Slate widget underneath, and the
canvas supports it. It is the whole integration — Folio links no CommonUI and behaves identically in a
project that has never heard of it.

`Set Keep Focus (true)` is the last resort, for a document with no activatable widget to hang that
override on. It reclaims focus whenever input arrives. Prefer the override: `SetKeepFocus` wins the
argument rather than avoiding it, and two things fighting over focus every frame is a bug waiting for
a reason to appear.

### What a focused document takes

Navigation and text only. A key the document has no meaning for falls through to the game, so a screen
that does not use the pause key does not swallow it. Escape and the console key always fall through,
whatever has focus.

Directional input is the exception, and deliberately: RmlUi reports a direction it could not act on —
already at the bottom of a list — as unconsumed, and letting that through would turn the pawn behind
the menu. So navigation keys are claimed whether or not they did anything.

## If something is wrong

- **Gamepad does nothing, keyboard works.** CommonUI took focus. See above. `folio.Input.Trace 1`
  logs every event and which document took it, which is how you tell "the pad is not reaching Folio"
  from "the pad is not reaching the game at all".
- **`{{ braces }}` render literally.** Nothing is bound under that name, or the bind happened after
  the document loaded. Add `ui/mock/<model>.json` and the document at least shows plausible values at
  design time.
- **The screen is there but nothing responds.** The canvas does not have focus. `Take Focus`, or the
  `GetDesiredFocusTarget` override.
- **Typing moves the player.** A text field does not have focus, so letters are being read as WASD
  navigation. Click or navigate into the field first. `folio.Nav.WASD 0` switches that scheme off for
  a game that would rather not have it at all.

## Related

- **[Documents in the world](world_documents.md)** — the same document on a surface in the level,
  which is a different component with its own input rules.
- **[Data binding](data_binding.md)** — the document side of view models.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md) §8** — every navigation key, the per-scheme switches,
  rebinding, and the confirm/back swap.

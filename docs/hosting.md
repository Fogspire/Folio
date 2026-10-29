# Hosting a document

`folio.Show` is for looking at something. Shipping a screen means putting a document inside whatever widget tree the game already has, and deciding who has focus. That is `UFolioWidget`.

Folio replaces UMG for *building* UI. This class exists for the boundary, not for building anything: a game keeps its HUD layout, its CommonUI stack, its menu flow, and puts a document inside it. There is no step where the shell has to be ported before the first screen can exist.

## Putting one in a widget tree

In the UMG designer the palette has **Folio Document** under Folio. Drop it in, set **Document Path** to the same kind of path `folio.Show` takes: `UI/screens/pause.rml`, relative to the project directory, or `/folio/...` to reach inside the plugin.

**Desired Size** is what the widget reports to Slate layout, not a limit on the document. A document is laid out to whatever size it ends up being given, so in a fill slot this value never comes up; it matters in a canvas panel or an auto-sized box.

From C++ or Blueprint, `Load Document` swaps what is up without rebuilding the widget.

## Showing game values

`Bind View Model (Name, Source, Poll Every Frame)` binds any `UObject`'s properties as a named data model. The document reads it the way it reads any model:

```html
<body data-model="player">
	<div class="bar" data-style-width="format(health / maxHealth * 100, 0) + '%'"></div>
</body>
```

```cpp
Document->BindViewModel(TEXT("player"), PlayerViewModel);
```

A source implementing `INotifyFieldValueChanged`, which `UMVVMViewModelBase` does, pushes changes as its fields change. One that does not is re-read once a frame instead, which costs work per bound field, so a view model is the better source when there is a choice.

**Bind before the document loads.** RmlUi resolves a document's bindings as it loads and offers no way to attach a model afterwards. `BindViewModel` is callable at any point, including before the widget is built (the widget defers loading until its bindings are in, and rebuilds the document if one is already up) so a Blueprint construction script is the natural place. What does not work is binding after the screen is visible and expecting `{{ braces }}` to start resolving.

[Data binding](data_binding.md) covers the document side.

## A HUD is not a screen

**Turn `Interactive` off for anything the player does not interact with.** A HUD is a readout drawn over a game that is still being played, and it must never be a focus target:

```cpp
Document->bInteractive = false;   // or SetInteractive(false) on one already up
```

Leave it on and the HUD is focusable, which breaks the game in a way that looks nothing like a UI bug: **alt-tab away and back, and the player can no longer move.** Slate restores focus to a focusable widget when the window is reactivated, the HUD is the widget it finds, and the pawn stops receiving input with nothing in the log to say why. It also swallows every click that lands on it.

Off makes the canvas unfocusable and hit-test invisible, so the mouse and the keyboard pass through to the game. Painting, HUD motion, data binding and hot reload are unaffected - the only thing it gives up is input, which a HUD never wanted.

**If input is still lost on alt-tab with the HUD non-interactive, the HUD is not what took it.** Anything else the game put on the viewport - a UMG widget added with `AddToPlayerScreen`, a CommonUI layer - is a candidate, and the cheapest way to stop hunting is to reassert focus when the window comes back:

```cpp
FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
    this, &AMyHud::OnActivationChanged);

void AMyHud::OnActivationChanged(const bool bIsActive)
{
    if (bIsActive)
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}
```

Do that as well as turning `Interactive` off, not instead of it: a focusable HUD still swallows clicks while the window is active.

## Panels over a world the player still clicks

An inventory or an action bar the player clicks while also clicking actors in the world: leave `Interactive` on and turn on **`Pass Through Empty Space`**. A press that hits no element goes to the game, with its release, and takes no focus. The wheel over a panel stays with the panel.

The document has to leave its empty space empty. `body` is an element, so a click on it counts as a hit:

```css
body   { pointer-events: none; }
.panel { pointer-events: auto; }
```

On the game side, use `FInputModeGameAndUI` with `SetHideCursorDuringCapture(false)`, and set `bShowMouseCursor` on the player controller. Actor `OnClicked` and `OnBeginCursorOver` also need `bEnableClickEvents` and `bEnableMouseOverEvents`.

**Every click still goes to the document:** the click landed on `body` or a full-screen wrapper. Find what is under the cursor with `folio.Debugger` and give it `pointer-events: none`.

## How big a HUD draws

`dp` resolves against a ratio, and **`Scale Follow`** decides how much that ratio moves with the viewport:

| | |
|---|---|
| `1` | Unreal's UI scale - literally `GetDPIScaleBasedOnSize` for the viewport, so the document sizes exactly as a UMG widget would. The default. |
| `0` | A constant pixel size. Resizing the window moves the corners without touching what is in them. |
| in between | Grows with the window, but less than proportionally. |

**Leave it at `1` unless a design asks for something else.** It matches every UMG widget in the same game, which is the behaviour players and designers already expect, and `folio.Screens` prints UMG's own answer beside the one in use so the two can be checked rather than assumed.

The dial exists because a corner-anchored HUD has a real argument against following: its dp box grows with the viewport, so a wide viewport pushes the health and ammo cards apart. Lowering it trades that against a HUD that covers more of a small window.

```cpp
Widget->ScaleFollow = 1.0f;
Widget->Scale = 1.0f;   // a multiplier on top, which a HUD scale slider writes
```

`Min Scale` and `Max Scale` bound the result, zero for no bound - a hard stop on what a 4K window is allowed to do.

## Smoothing the edges

A background carries its own antialiasing: a one pixel skirt of coverage on its outer boundary, which is exactly what a browser does and is exact. It is also the least that can look smooth. A shallow edge - a slanted card, a long diagonal, a wide curve - advances one pixel every six or more, so the straight run between two steps is visible however accurate the blend is. Widening the skirt with `folio.Antialias.Width` trades a hard step for a blurry one rather than fixing it.

**`Supersample` is the fix.** Set it to 2 and the document is drawn into a target twice the size each way and scaled down - four samples per pixel on every edge.

```cpp
Widget->Supersample = 2;
```

Nothing about the document changes. It lays out, shapes and hit-tests in screen pixels at every sample count; what gets bigger is the target, and the replay stretches the one onto the other. So the sample count is not a layout decision and cannot move a line break, and `folio.Screens` reports the same context size whatever it is set to.

**Text does not go through the target.** Glyphs are distance fields - sharp at any size already - and a resample can only soften them, which is visible on a menu and unmissable on body copy. So a glyph run drawn straight into a supersampled document is held back and drawn to the screen afterwards, at one document pixel to one screen pixel. The text is the text an unsupersampled document draws, to the pixel, while the panel behind it gets its four samples an edge.

Two cases put a run back through the target, both because it belongs to something that reads it back:

- text inside a `filter`, `box-shadow`, `mask-image` or any other element that pushes a layer;
- text under a clip mask, which is a stencil on the surface and not something Slate can express.

Text that something else in the same document paints over goes back too - a run is only held out if nothing covers it, which is checked per draw rather than assumed. A document that finishes with a full-screen sheen, scrim or vignette therefore keeps all of its text in the target, because all of it is painted over; `folio.Screens` reporting zero native runs on a screen like that is the mechanism working, not failing.

`folio.Supersample.NativeText 0` turns the whole thing off and puts every run through the target, which is what to compare against if text looks wrong. `folio.Screens` reports the sample count and how many runs the last paint drew natively; zero on a screen with words on it means something is covering all of them.

What it costs:

- A render target of the viewport times the sample count, squared - and another for every layer the document pushes for a shadow or a filter, held between frames. At 1080p and 2x that is 33MB each; at 4K and 4x it is 531MB each, which is where the operating system starts warning about memory.
- One resolve pass and one composite a frame.

So it is per document rather than a project setting: on for a menu or a HUD with slanted panels, off for one with nothing but text on it or a tight memory budget. **Two is the sample count worth paying for; four is sixteen times the pixels for an edge nobody sees.** **[Supersampling](supersampling.md)** is the whole of it: what it must not change, the three ways of getting it wrong that all look like it making the picture worse, and the measurements behind that.

`folio.Screens` reports what every screen document is laid out against: pixels, the ratio, what UMG would use, and what the document actually covers. **`drawn` has to match `px`**; when it does not, the document lands short of one corner and past the other, and no amount of stylesheet work will fix it.

A context unit is a *pixel*, not a dp. RmlUi converts dp lengths itself using the ratio, so anything that scales the drawn geometry by the ratio as well squares it - correct at the resolution the ratio happens to be 1, and wrong at every other. If a document looks right at 1080p and grows or shrinks twice as fast as it should elsewhere, that is what happened.

Everything below is about screens the player *does* interact with.

## Focus

This is the part that goes wrong silently, so it is worth understanding rather than copying.

A document only receives navigation and text while its canvas holds Slate focus. `Take Focus` moves focus to it. In a project with no CommonUI, that is the whole story.

### With CommonUI

Keep your activatable widgets. A screen can be a `UCommonActivatableWidget` whose whole tree is one **Folio Document**: the activatable stack still pushes and pops it, applies its input config, and handles back, and the widget can bind CommonUI actions and answer them in C++ or Blueprint. That makes it a natural input handler for the document, with the document drawing the screen and the widget around it doing nothing else.

The one thing to add is the focus target below.

CommonUI's action router asks its leaf-most activatable widget for a `GetDesiredFocusTarget()`, and when it gets none it can use, it focuses **the game viewport instead**: silently taking focus off a document that had it. Worse, it reapplies its input config on its own schedule, so a document can hold focus perfectly and then lose it later with nothing in the log to say why.

The symptom is a gamepad doing nothing at all while the same screen works fine from the keyboard. Nothing about that points at CommonUI.

Return the widget from the override and it is solved:

```cpp
UWidget* UMyScreen::GetDesiredFocusTarget() const override
{
	return Document;
}
```

That works because `UWidget::SupportsKeyboardFocus` defers to the Slate widget underneath, and the canvas supports it. It is the whole integration: Folio links no CommonUI and behaves identically in a project that has never heard of it.

`Set Keep Focus (true)` is the last resort, for a document with no activatable widget to hang that override on. It reclaims focus whenever input arrives. Prefer the override: `SetKeepFocus` wins the argument rather than avoiding it, and two things fighting over focus every frame is a bug waiting for a reason to appear.

### What a focused document takes

Navigation and text only. A key the document has no meaning for falls through to the game, so a screen that does not use the pause key does not swallow it. Escape and the console key always fall through, whatever has focus.

Directional input is the exception, and deliberately: RmlUi reports a direction it could not act on (already at the bottom of a list) as unconsumed, and letting that through would turn the pawn behind the menu. So navigation keys are claimed whether or not they did anything.

## If something is wrong

- **Gamepad does nothing, keyboard works.** CommonUI took focus. See above. `folio.Input.Trace 1` logs every event and which document took it, which is how you tell "the pad is not reaching Folio" from "the pad is not reaching the game at all".
- **`{{ braces }}` render literally.** Nothing is bound under that name, or the bind happened after the document loaded. Add `UI/mock/<model>.json` and the document at least shows plausible values at design time.
- **The screen is there but nothing responds.** The canvas does not have focus. `Take Focus`, or the `GetDesiredFocusTarget` override.
- **Typing moves the player.** A text field does not have focus, so letters are being read as WASD navigation. Click or navigate into the field first. `folio.Nav.WASD 0` switches that scheme off for a game that would rather not have it at all.

## Related

- **[Documents in the world](world_documents.md)**: the same document on a surface in the level, which is a different component with its own input rules.
- **[Data binding](data_binding.md)**: the document side of view models.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md) §8**: every navigation key, the per-scheme switches, rebinding, and the confirm/back swap.

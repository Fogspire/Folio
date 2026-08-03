# Input and navigation

Keyboard, gamepad and mouse all work out of the box. A document never learns what produced a
direction - arrows, WASD, IJKL, d-pad and either analog stick all arrive as the same four keys, and
all of it is live at once.

| | |
|---|---|
| Arrow keys, **WASD**, **IJKL** | up, down, left, right |
| D-pad, and either stick pushed to its extreme | the same four |
| Either analog stick, held | repeats: 0.5 deadzone, 0.4s before the first repeat, 0.12s after |
| `Enter`, `Space`, gamepad **confirm** face button | activate the focused element |
| `Escape`, the other face button | back |
| `Tab`, either shoulder | tab order |

**Letters only navigate when nothing is taking text.** With an `input` or `textarea` focused, W is a
W. That is what lets three keyboard schemes coexist without any of them breaking typing.

## The one rule everything follows from

**`nav` is read from the focused element itself, and it does not inherit.**

When a direction key arrives, RmlUi reads `nav-<direction>` as a *local* property of the element that
currently has focus. A container's `nav` is never consulted, and `nav` set on an ancestor is never
inherited down.

```css
/* Does nothing at all. */
.menu { nav: vertical; }

/* Correct: the property goes on the focusable items. */
.menu-item { tab-index: auto; nav: vertical; }
```

Three `tab-index: auto` items inside a `.list { nav: vertical; }` container navigate exactly once,
into the first item, and then stop dead. If your menu moves one step and freezes, this is why.

**Every focusable element carries its own `nav`**, declared next to its `tab-index: auto` in whatever
rule defines the component. The shipped library does this; anything you write must too.

## Making something focusable

`tab-index: auto` makes an element focusable, **and nothing else does.** It is a flag, not an
ordering - Tab walks document order.

```css
.menu-item { tab-index: auto; nav: vertical; }
```

`<button>` is not a button. Only `body`, `img`, `handle`, the form controls, `progress` and `tabset`
are real element types; everything else - including `button` - is a generic element with no focus
behaviour until it gets `tab-index: auto`. See **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §11.

**Enter, numpad Enter and Space on a focused element synthesise a click.** You get keyboard and
gamepad activation for free, and you should not add a separate keydown path for it.

## `nav` values

`none`, `auto`, `horizontal`, `vertical`, or an `#id` string.

`horizontal` and `vertical` are filters on the **incoming direction**, not on where the search looks.
`nav-up: vertical` passes; `nav-up: horizontal` blocks. So the useful form is the single value:
`nav: vertical` on a menu item means up and down navigate and left and right do nothing.

**`nav` is a box shorthand** in `up right down left` order, exactly like `margin`. Which makes
`nav: vertical horizontal` a trap: it reads as up/down `vertical` and left/right `horizontal`, every
one of which passes its own filter - an elaborate way of writing `nav: auto`.

**Axis locking stops focus leaving as well as entering.** A dialog's action row wants `nav: auto` so
up returns to the content; a long settings list wants `nav: vertical` so a stray left press does not
land in the sidebar. Choose per component, not globally.

**`nav: auto` on `body`.** The document is the focus holder before anything is focused and after
focus is lost, and this is what lets the first direction press enter the content. Without it a fresh
document is unreachable by controller, and there is no recovery after a mouse click lands on nothing.

**Explicit overrides come in pairs.** `nav-down: #b` on `a` does not give you `nav-up: #a` on `b` -
navigation is resolved entirely from the source element. A missing id warns at load.

## Two traps worth knowing before you hit them

**Focus will not leave a region.** The spatial search starts from the focused element's **nearest
scroll container**, and any element whose `overflow` is not `visible` on either axis is one. An
incidental `overflow: hidden` on a layout wrapper confines focus somewhere you did not intend, and it
reads as a navigation bug rather than a styling one. **Look for `overflow` first.**

**A modal is only half-trapped by that.** `overflow: hidden` keeps navigation inside the dialog once
focus is there, but nothing puts it there. A fresh document has nothing focused, and the first
direction press runs the *document's* `nav`, which takes the first tabbable element in document order
- the content behind the dialog.

Make the background inert for as long as the dialog is up:

```css
.screen.is-behind-modal { focus: none; pointer-events: none; }
```

`focus: none` inherits down the whole subtree; `pointer-events: none` takes it out of hit testing.
Without both, a scripted `down,right,up,left` walks the buttons behind the dialog and never reaches
it.

## Focus styling

**Style `:focus-visible`, never `:focus`.** `:focus-visible` is set by navigation actions
specifically, so the focus ring correctly stays hidden during mouse use.

```css
.focusable:focus-visible { border-color: #f0c674; box-shadow: #f0c67459 0dp 0dp 12dp; }
```

`--a11y` checks that focusing an element changes something visible, and fails the build if it does
not. See **[accessibility](accessibility.md)**.

## Disabled

Disabled controls want all three:

```css
.is-disabled { opacity: 0.6; color: #8a8a99; focus: none; pointer-events: none; }
```

A class for the look, `focus: none` to leave navigation, `pointer-events: none` to stop responding.

`:disabled` is set **only** by `input`, `select` and `textarea`, and only from the `disabled`
attribute. On a `<button>` or a `<div>` it matches nothing and warns about nothing. So: real form
controls use the attribute, everything else uses a class.

Note that `focus: none` also suppresses `click`, because a click is dispatched to the nearest
*focusable* ancestor rather than to what was under the cursor - so a `focus: none` element silently
forwards its clicks to whatever contains it. `mousedown`, `mouseup`, `mouseover` and `:hover` still
fire on it.

## Switching schemes off

Each scheme is a switch, settable from `DefaultEngine.ini` or the console with no rebuild:

`folio.Nav.Arrows` · `folio.Nav.WASD` · `folio.Nav.IJKL` · `folio.Nav.Gamepad`

A game that binds WASD to movement while a HUD is up wants that one off. Turning the arrows off still
leaves them working inside a text field, where they mean something else.

## Binding your own keys

For a game that wants its own scheme, or a rebinding screen. Keys map to **actions**: `Up`, `Down`,
`Left`, `Right`, `Confirm`, `Back`, `NextTab`, `PreviousTab`.

```cpp
// A UDataTable of FFolioNavBinding rows (Key, Action).
UFolioNavLibrary::LoadNavBindings(Table, /*bReplaceDefaults*/ true);

// Or one at a time, which is what a rebinding screen does.
UFolioNavLibrary::SetNavBinding(EKeys::Q, EFolioNavAction::Left);
UFolioNavLibrary::GetKeysForNavAction(EFolioNavAction::Left);  // to show the player
```

With `bReplaceDefaults` the table becomes the **entire** mapping. Without it the rows are additions,
so arrows and the pad keep working alongside them.

**A binding wins over the built-in schemes and ignores the `folio.Nav.*` switches** - a project that
bound a key meant it.

All of it is reachable from Blueprint, and from the console for trying one out: `folio.Nav.Bind Q Left`,
`folio.Nav.Bindings`, `folio.Nav.ResetBindings`.

## Which face button confirms

South (A, Cross) confirms by default and east (B, Circle) goes back. Nintendo hardware and Japanese
conventions are the other way round, so it is a setting rather than a constant:

```cpp
FFolioModule::Get().SetConfirmButton(Folio::EConfirmButton::East);
```

or `folio.Nav.SwapConfirmBack 1`. Changing it swaps what the two buttons *do* and updates every open
document in the same frame.

**Folio has no platform opinion of its own** - it does not check what it is running on, because the
answer depends on region as much as hardware. A port decides and calls the setter.

## Telling a document what the player is holding

Folio tracks the last device used process-wide, so a HUD and a pause menu open together cannot
disagree. Most UI only needs the body classes:

| | |
|---|---|
| `folio-device-keyboard` `folio-device-mouse` `folio-device-gamepad` | exactly one, the precise device |
| `folio-input-kbm` `folio-input-gamepad` | exactly one, the coarse question glyph swapping actually asks |
| `folio-confirm-south` `folio-confirm-east` | exactly one, which button confirms |

```css
.prompt { display: none; }
body.folio-input-gamepad .prompt.pad { display: inline-block; }
body.folio-input-kbm     .prompt.key { display: inline-block; }

/* A focus ring is noise under a mouse and essential without one. */
body.folio-device-mouse .menu-item:focus-visible { border-color: transparent; }
```

When an expression has to *build* a value, the reserved `folio` data model has `device`, `gamepad`,
`confirm` and `back`:

```html
<div data-model="folio">
	<img data-attr-sprite="'btn_' + confirm"/>
	<span data-if="gamepad">hold to skip</span>
</div>
```

`folio` is a **reserved model name - do not use it for game data.**

Both routes are pinned in the preview with `--input-device keyboard|mouse|gamepad` and
`--confirm-button south|east`, which is how a gamepad screenshot is taken without a gamepad.

## Sounds

**UI sounds are reported, not played.** `FFolioModule::OnUiSound()` fires `Focus`, `Confirm`, `Back`
or `Invalid`. Folio owns no sound assets. `Invalid` is a click on something carrying `.is-disabled`,
which is the case worth a different noise.

## If the gamepad does nothing

Look for this in the log:

```
LogUIActionRouter: [User 0] No focus target for leaf-most node [...], and the widget
                   isn't focusable - focusing the game viewport.
```

CommonUI's action router owns focus, and when it reapplies its input config it silently takes focus
off a document that had it. One override fixes it for good:

```cpp
UWidget* UMyScreen::GetDesiredFocusTarget() const override { return Document; }
```

See **[hosting a document](hosting.md)**. Folio links no CommonUI and behaves the same in a project
that has never heard of it - the integration is entirely on the game's side.

## If the gamepad confirms the wrong control

CommonUI turns the pad's accept button into a **left mouse click at its software cursor**, so a
confirm presses whatever the cursor happens to be sitting over rather than what is focused. It looks
exactly like a focus bug and is not one: the giveaway is that keyboard enter works, the pad does not,
and Folio's input trace shows nothing at all for the press because it never arrives as a key.

Three console variables, all defaulting on, in `DefaultEngine.ini`:

```ini
[ConsoleVariables]
CommonUI.ShouldVirtualAcceptSimulateMouseButton=0
CommonUI.EnableVirtualPointer=0
CommonUI.SyncCursorToFocusedWidgetOnInputMethodChange=0
```

The second turns off the software cursor itself. Leave it on and the stick drives the pointer, which
means a player holding only a pad is generating real mouse movement - so hover follows the stick, and
anything keyed off which device is in use flickers between pad and mouse several times a second.

The third moves the cursor to the centre of the focused widget every time the player picks up the
pad, so that the fake accept click lands on the right control. The focused widget is the canvas, so
the centre of it is the centre of the screen, and Slate synthesises a mouse move to get there - far
enough that Folio reads it as the player taking the mouse back and hands the device straight to the
pointer again. With the first line off there is no fake click left to aim.

## Binding a key while a screen is up

A focused document is where Slate routes input, so a player controller's own bindings do not fire
while one is open. A menu button that should close the menu is the usual casualty.

Folio consumes only what a document claims - navigation, confirm and back, whether RmlUi could act on
them or not, so none of those reach the pawn behind the screen. Everything else is offered back:

```cpp
Document->OnUnclaimedKey.AddDynamic(this, &AMyHud::HandleUnclaimedKey);

void AMyHud::HandleUnclaimedKey(const FKey Key)
{
	if (Key == EKeys::Gamepad_Special_Right)
	{
		CloseMenu();
	}
}
```

`OnBack` is the same idea for escape and the pad's back button, which Folio does claim.

## Where to go next

- **[Hosting a document](hosting.md)** — the widget-tree boundary and the focus override.
- **[Components and themes](components_and_themes.md)** — the library already carries its `nav`.
- **[Accessibility](accessibility.md)** — the focus-visibility check.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §8 — the full reference.

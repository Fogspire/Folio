# Modals and focus trapping

A dialog that takes over the screen is the one interaction everybody gets wrong the first time, and
the failure is specific: the dialog looks right, the mouse works, and the gamepad walks straight
through it into the buttons behind.

## The short version

```html
<div class="screen" data-class-is-behind-modal="ShowConfirm">…</div>

<div class="modal-scrim" data-if="ShowConfirm"></div>
<div class="modal" data-if="ShowConfirm">
	<h2 data-loc="confirm_title">Delete save?</h2>
	<p data-loc="confirm_body">This cannot be undone.</p>
	<div class="modal-actions">
		<button class="btn btn-danger focusable" id="delete" data-loc="confirm_delete">Delete</button>
		<button class="btn focusable" id="cancel" autofocus data-loc="confirm_cancel">Keep</button>
	</div>
</div>
```

```css
/* Everything behind the dialog goes inert - both properties, for two different reasons. */
.screen.is-behind-modal { focus: none; pointer-events: none; }

.modal-scrim { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.modal       { position: absolute; overflow: hidden; }
```

Four things are doing work there, and all four are load-bearing.

## Why `overflow: hidden` on the dialog

The spatial focus search starts from the focused element's **nearest scroll container**, and any
element whose `overflow` is not `visible` on either axis is one.

So `overflow: hidden` on the dialog is what keeps navigation *inside* it once focus is there. Without
it, a down press from the last button escapes into whatever is behind.

This is the same rule that bites accidentally elsewhere: an incidental `overflow: hidden` on a layout
wrapper confines focus somewhere you did not intend. **If focus refuses to leave a region, look for
`overflow` first.**

## Why that is only half of it

`overflow: hidden` keeps focus in once it is there. **Nothing puts it there.**

A fresh document has nothing focused. The first direction press runs the *document's* `nav`, which
takes the first tabbable element in document order — the content behind the dialog. Verified: without
the rest of this, a scripted `down,right,up,left` walks the buttons behind the dialog and never
reaches it.

Two fixes, and you want both.

**`autofocus` on the button you want.** Put it on the safe choice, not the destructive one: `Keep`,
not `Delete`.

**`focus: none` on the background.** It inherits down the whole subtree, so one class on the screen
root makes everything under it unfocusable.

## Why `pointer-events: none` as well

`focus: none` handles navigation. It does not stop a click.

It has a subtler effect worth knowing: `focus: none` **suppresses `click`**, because a click is
dispatched to the nearest *focusable* ancestor rather than to what was under the cursor. So a
`focus: none` element silently forwards its clicks to whatever contains it. `mousedown`, `mouseup`,
`mouseover` and `:hover` still fire on it.

`pointer-events: none` is what takes an element out of hit testing entirely. That is the one you want
on a modal background.

## The scrim

```css
.modal-scrim { background-color: #0a0a0ec2; }
```

A flat wash is enough. If you want the screen to go soft behind the dialog:

```css
.modal { backdrop-filter: outside-blur(8dp); }
```

`outside-blur` is Folio's own — where `blur` softens what is *behind* an element, this softens
everything the element is *not*, which is exactly a modal scrim. It works in both renderers and it
reaches outside the UI, so the 3D scene blurs with it.

Two limits: the hole is a rectangle, so a `border-radius` dialog still cuts a square one, and it costs
four blurred regions rather than one.

## Back and cancel

`Escape` and the gamepad's back button raise a back action. Nothing in Folio closes anything — the
document does not own the decision:

```html
<div class="modal" data-event-keydown="ShowConfirm = false"></div>
```

or listen in C++. See **[calling into game code](hosting.md)**.

## Stacking two

A second modal over the first needs the first to go inert too, by the same rule. Put the class on
every layer that is not the top one rather than only on the screen root — which is an argument for
driving `is-behind-modal` from one piece of game state that knows how deep the stack is, rather than
from each dialog.

## Checking it

```bash
uipreview ui/screens/confirm.rml --root . --keys "down,right,up,left" --shot-each out/
```

If any shot shows focus behind the dialog, the trapping is wrong. This is worth doing every time —
it is very hard to spot by looking, because the mouse path works perfectly throughout.

`rcss/recipes/confirm.rml` is a working dialog with all of this in it.

## Where to go next

- **[Input and navigation](input_and_navigation.md)** — the `nav` rules everything here follows from.
- **[Components, themes and recipes](components_and_themes.md)** — the modal component and the recipe.
- **[Accessibility](accessibility.md)** — a dialog is the case where focus visibility matters most.

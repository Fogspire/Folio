# A CRT terminal in the world

Build a working computer terminal on a curved CRT screen in a level: a login form you can type into,
radio channels, a pop-up window you can drag, resize and close with an animation, and a bin you can
drop files into. Then put it on a mesh with scan lines and a phosphor glow.

It is one document and one component. No Blueprint graph, no widget tree, and the only C++ is
pointing an input action at the component.

The finished thing is `dev/crt_terminal.rml`. Open it beside this page.

```bash
./bin/win64/uipreview.exe dev/crt_terminal.rml --root . --size 1200x900
```

---

## 1. The shape of the problem

The screen is an **oval**, wider than tall. The document is a **rectangle**, because a render target
and a projection both are.

So the corners of the document simply are not on the mesh, and nothing may be laid out there. That is
the constraint the whole layout is built around, and it is worth stating plainly because it is not
obvious from looking at either the mesh or the markup.

RCSS `border-radius` takes lengths only - **no percentages** - so you cannot make an ellipse by
rounding a box. The oval is a radial falloff instead, which reads as glass on the mesh and as an oval
on a flat surface.

```css
body {
	font-family: Roboto Mono;
	font-size: 32dp;
	color: #7dff9b;
	background-color: transparent;
	width: 100%;
	height: 100%;
	box-sizing: border-box;
	position: relative;
	overflow: hidden;
}
```

`width: 100%` and `height: 100%` both matter. A document is laid out shrink-to-fit against its
context regardless of `display`, so without them the body is content-height and the background stops
partway down.

Content sits inside the ellipse's inscribed rectangle, which is `1/sqrt(2)` of each axis - about 70%:

```css
.screen {
	position: absolute;
	left: 15%; top: 15%;
	width: 70%; height: 70%;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
}
```

Then each row gets its **own** width rather than sharing one column width, so the lines nearest the
top and bottom stay clear of the curve instead of running into it:

```css
.narrow { width: 62%; }
.mid    { width: 84%; }
.wide   { width: 100%; }
```

That is the whole trick to making a rectangle look like it belongs on an oval: a lens-shaped block of
text, not a centred rectangle.

---

## 2. Text you can type into

```html
<div class="wide row">
	<span class="label">OPERATOR</span>
	<input type="text" class="field" id="operator" maxlength="24"/>
</div>
<div class="wide row">
	<span class="label">ACCESS</span>
	<input type="password" class="field" id="access" maxlength="16"/>
</div>
```

`input` is one of the few real element types. Caret, selection, editing, clipboard - all of it comes
from RmlUi, and none of it is world-space specific. A field on a monitor in a room behaves exactly
like a field on a menu.

Style the caret with `caret-color`, and the focus state with `:focus-visible` - never `:focus`, so
the ring stays hidden during mouse use.

**While a field has focus, letters are letters.** Folio's WASD and IJKL navigation schemes stand down
automatically, which is what lets three keyboard schemes coexist without breaking typing. See
**[input and navigation](../input_and_navigation.md)**.

---

## 3. Channels and buttons

```html
<div class="channel">
	<input type="radio" name="channel" id="ch_a" checked/>
	<span>ALPHA</span>
</div>
```

Radios share a `name` and RmlUi handles the exclusivity. The label is the sibling `<span>`, which is
also what makes the control count as *named* for the accessibility check - see
**[accessibility](../accessibility.md)**.

```html
<button id="engage">ENGAGE</button>
```

**`<button>` is not a button.** It is a generic element like `div`, with no focus behaviour and no
click handling until it gets `tab-index: auto`:

```css
button { tab-index: auto; nav: auto; }
```

With that, Enter and Space and the gamepad confirm button all synthesise a click for free.

---

## 4. A window you can move and resize

No code:

```html
<div id="win" class="window">
	<handle class="titlebar" move_target="win">DISK UTILITY</handle>
	<div class="close" folio-close="win" folio-label="Close">X</div>
	<div class="window-body">…</div>
	<handle class="grip" size_target="win"></handle>
</div>
```

```css
/* Absolute, because a handle writes left, top, width and height, and those mean nothing to a
   flex item. */
.window { position: absolute; left: 12%; top: 22%; width: 420dp; height: 260dp; }
.grip   { position: absolute; right: 0; bottom: 0; width: 20dp; height: 20dp; }
```

`<handle>` is a real element type. `move_target` moves the element with that id, `size_target`
resizes it, and neither needs a line of C++.

---

## 5. Icons you can drag, and a bin

```html
<div id="icon_a" class="desk-icon" drag="drag-drop" folio-drag="move" style="left: 28dp; top: 66dp;">
	<span class="glyph"></span>CORE.DAT
</div>

<div class="bin" folio-drop="remove">
	<span class="glyph"></span>RECYCLE
</div>
```

Two things here are the whole of what people get wrong:

**`drag: drag-drop`, not `drag: drag`.** RmlUi only looks for a drop target when the dragged
element's `drag` is `drag-drop` or `clone`. With plain `drag` the bin never hears about it and
`folio-drop` never runs.

**`folio-drag="move"` is what moves it.** `drag` alone fires events and leaves the element where it
was. The element must be `position: absolute`, because `folio-drag` writes `left` and `top`.

Then style the states, or a player cannot tell a drop will land:

```css
.desk-icon.folio-dragging { opacity: 0.55; }
.bin.folio-dragover       { border-color: #7dff9b; background-color: #7dff9b1a; }
```

`folio-drop` and `folio-close` are looked for **up the tree**, so dropping on the bin's glyph or its
label is dropping on the bin. Put the attribute on the box you think of as the thing.

---

## 6. Animating things out

Both the close and the drop set `folio-closing` and **wait** for an animation before removing
anything:

```css
@keyframes consume { from { transform: scale(1); opacity: 1; }
                     to   { transform: scale(0.05); opacity: 0; } }
.desk-icon.folio-closing { animation: 0.35s cubic-in consume; }

@keyframes suck { to { transform: translateY(520dp) scale(0.04); opacity: 0; } }
.window.folio-closing { animation: 0.5s cubic-in suck; }
```

Without an animation on the class, the element goes at once.

**One thing that only bites on a surface:** the render target *is* the document, so anything
translated past its edge is clipped, and text that leaves the context is culled outright. An exit
animation that slides off screen looks like it vanishes partway. Shrink towards an edge rather than
travelling past it - which is what `suck` does.

Two more animation rules that are easy to get wrong and never warn:

- **Times are seconds. `ms` does not exist.** `0.35s`, never `350ms` - the latter parses as a
  reverse adjustment factor and produces no animation at all.
- **The easing names are RmlUi's.** `cubic-in`, `cubic-out`, `cubic-in-out`. There is no `ease-out`;
  a misspelled name is read as a property name and silently never fires.

---

## 7. The CRT look

Three stacked overlays, all `pointer-events: none` so they never eat a click:

```css
.scanlines { decorator: shader("scanlines #7fe8ff90 0.004 0.5"); }
.vignette  { decorator: radial-gradient(circle farthest-corner at 50% 50%, #00000000 60%, #000000b0); }
.roll      { /* a slow vertical sweep, animated with transform: translateY */ }
```

`decorator: shader(...)` reaches the three effects RCSS has no syntax for - `additive`, `scanlines`
and `sweep`. They composite premultiplied, so a colour with zero alpha *adds* to what is under it,
identically in both renderers.

The glow on the title is a font effect, not a shadow - `text-shadow` does not exist:

```css
.title { font-effect: glow(3dp #2bff7a80); }
```

Remember the reach cap: a font effect reaches about **0.104em**, so 3dp of glow on 32dp text is well
inside it and 3dp on 13dp text would be clamped with a warning. See
**[text and fonts](../text_and_fonts.md)**.

---

## 8. Onto a mesh

Add a **Folio World Document** component to an actor with the CRT mesh.

| | |
|---|---|
| `Document Path` | `dev/crt_terminal.rml` |
| `Draw Mode` | `Surface` |
| `World Size` | the screen's size in world units - for a 135.97 × 107.314 monitor, that |
| `Pixels Per Unit` | pick so `WorldSize × PixelsPerUnit` lands near your target resolution |

`Surface` is the mode to use here, and the reason is specific: it is the only one with layer effects.
The vignette gradient, any `filter` and any `box-shadow` all need an offscreen target. The price is
one resample, which is why it is not the default.

Then bind the target to a material. `/Folio/M_FolioSurface` ships with the plugin - instance it, and
feed its **Element** texture parameter from the component's `OnSurfaceChanged` event.

Full setup, including the material domain and the crash you get from an oversized
`Pixels Per Unit`: **[documents in the world](../world_documents.md)**.

### Scan lines in the material

The document's scan lines are a decorator, drawn flat. To get scan lines that bend with the glass and
roll independently of the UI, do it in the material instead - which is what the shipped
`M_FolioSurfaceCRT` node export does.

The graph, in order:

1. **TextureCoordinate** — `CoordinateIndex 1`, `VTiling 3`. UV1 so it is independent of the
   element's own UVs, tiled three times down the screen.
2. **Panner** — that coordinate, `Time` from a **Time** node, `Speed` from a **MakeVector2** whose Y
   is a **ScalarParameter `ScanLineSpeed`**. Panning only in V, at a speed the material instance sets.
3. **TextureSampleParameter2D `ScanLines`** — the panned coordinate, sampling a `crt_lines` texture.
4. **Multiply** — its red channel by a **ScalarParameter `ScanLineAlpha`**, so the effect has a
   strength dial.
5. **Lerp** — `A = 1`, `B = 0`, `Alpha =` that multiply. This turns the scan-line mask into an
   exponent that is 1 where there is no line and 0 where there is.
6. **Power** — `Base =` the **Element** sample's RGB, `Exp =` that lerp.

Driving an exponent rather than multiplying is what makes it look like a CRT rather than like a
striped overlay: darkening by a power curve keeps bright phosphor bright and eats the dim areas,
which is what the scan gap actually does.

The node export is at `dev/M_FolioSurfaceCRT.nodes.txt` - copy its contents and paste directly into a
material graph.

---

## 9. Pointing input at it

The component takes pointer and keyboard input, but something has to tell it where the pointer is.

```cpp
// Drive the pointer from where the player is looking.
Terminal->MovePointerToHit();

// Then the usual press and release from your input action.
Terminal->PressPointer();
Terminal->ReleasePointer();

// And hand it the keyboard so the fields can be typed into.
Terminal->FocusDocument();
```

The properties that matter:

| | |
|---|---|
| `Pointer From Player View` | trace from the camera rather than from a supplied position |
| `Pointer Uses Mesh UV` | read the hit's UV rather than projecting onto the component's plane. **On** for a curved screen |
| `Invert Pointer V` | UV conventions differ by exporter. If the pointer is mirrored vertically, this is the fix, not a curvature problem |
| `Show Cursor` | draw a cursor on the surface, since the OS one is somewhere else entirely |

`Release Focus` gives the keyboard back to the game.

**If the pointer is consistently offset**, check `Invert Pointer V` before anything else. It is the
answer far more often than the geometry is.

`folio.World.ShowSurface 1` logs what the pointer is doing, which is how to tell "nothing is driving
the pointer" from "the pointer is in the wrong place".

---

## 10. Check it

```bash
./bin/win64/uipreview.exe dev/crt_terminal.rml --root . --strict --a11y
```

Both clean on the shipped document. Then in the editor:

| | |
|---|---|
| `folio.Reload` | rebuild from disk without leaving PIE |
| `folio.World.Verify` | confirm the world path is drawing what it should |
| `folio.Surface.Shot` | write the surface render to a PNG, comparable with the preview's |

---

## What you have learned

Everything in the vocabulary, in one document:

- layout under a non-rectangular constraint (§1)
- real form controls and focus (§2, §3)
- `<handle>` move and resize with no code (§4)
- `drag-drop`, `folio-drag`, `folio-drop` and the state classes (§5)
- animated removal that the engine waits for (§6)
- shader decorators and font effects (§7)
- a document on a mesh, and a material that treats it as a screen (§8)
- pointer and keyboard into a world document (§9)

## Where to go next

- **[An inventory screen](inventory.md)** — the same drag vocabulary against a data model.
- **[Documents in the world](../world_documents.md)** — the other two draw modes and when to use them.
- **[Nameplates and markers](../markers.md)** — the screen-space counterpart.

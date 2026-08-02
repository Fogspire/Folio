# The coloured screen edge

Standing in fire and the corners of the screen glow orange. Something bites you and the whole frame
pulses red for a tenth of a second. Health drops under a quarter and a dark red haze sits at the edge
until you heal. Every game does some version of this, and it is the cheapest legibility you can buy:
it is readable in peripheral vision, it never covers what the player is aiming at, and it needs no
art.

**It is one property.** `box-shadow` with the `inset` keyword paints a soft falloff inward from the
element's own border, and an element that is the whole view is a vignette.

```css
#hazard {
	position: absolute;
	left: 0dp; top: 0dp; width: 100%; height: 100%;
	border-radius: 90dp;
	box-shadow: 0dp 0dp 55dp 55dp #f2551b8f inset;
}
```

```bash
./bin/win64/uipreview.exe examples/screen_edge.rml --root . --strict --screenshot out/edge.png
```

---

## 1. Why not a gradient

The obvious first attempt is `decorator: radial-gradient(...)` on a full-screen element. It works, and
it is wrong for this, because a radial gradient is an **ellipse**. On a 16:9 view the falloff reaches
much further in at the top and bottom than at the sides, and the result reads as a lens rather than
as the edge of a screen.

The second attempt is a stack of concentric frames with `border-width` and increasing alpha, faking a
ramp. Do not do this. It takes eight elements to stop looking like stripes, it still has visible
banding on a flat background, and it is strictly worse than the one property that already exists.

`box-shadow ... inset` follows the **rounded rectangle**, which is the shape of the screen, and its
falloff is a real blur.

## 2. The four numbers

```
box-shadow: <offset-x> <offset-y> <blur> <spread> <colour> inset;
```

| | What it does here |
|---|---|
| offset | **Zero.** An offset edge effect reads as a light source, not as a state. |
| blur | How soft. Below about 60dp it starts to read as a border. |
| spread | How far in the solid part reaches before the blur begins. This is the dial for "how much screen does this take". |
| colour | Carry the intensity in the **alpha**, not by dimming the hue. A washed-out orange at full alpha looks like a dirty screen; a saturated orange at `8f` looks like fire. |

Blur and spread compound: the effect reaches roughly `spread + blur` inward, and the first `spread`
of that is at full strength. A hit flash wants a large spread so it is unmissable; a standing hazard
wants a large blur and a small spread so it stays out of the way.

## 3. Two limits on a full-screen one

Both of these are specific to an element the size of the view, and both fail by drawing the effect in
the wrong place rather than by warning.

**Spread must be non-zero.** `0dp 0dp 60dp 0dp <colour> inset` renders nothing at all. Blur alone does
not produce a shadow; something has to be there for it to soften.

**Blur must stay small.** Past about 70dp the whole effect displaces toward the bottom right, and by
120dp the top and left edges have lost it entirely while the bottom and right still have it. Measured
on a 1920x1080 view, one element, no transform:

| blur | top | bottom | left | right |
|---|---|---|---|---|
| 30dp | 122 | 122 | 64 | 122 |
| 70dp | 37 | 62 | 10 | 62 |
| 120dp | 0 | 35 | 0 | 35 |
| 200dp | 0 | 20 | 0 | 20 |

So **reach comes from spread, and blur only softens the inner edge**. `55dp` blur with `55dp` spread
covers 110dp with a soft inside; `120dp` blur with `20dp` spread covers nothing on two of the four
edges. The remaining left-edge deficit in the first row is the outermost two or three pixels only, and
is not visible.

Do not try to fix the corners by hanging the element off the view either. An inset shadow on an
element larger than the viewport lands misplaced the same way, pulled toward whichever corner it
overhangs - which looks exactly like the HUD being anchored top-left and running off the bottom right.

That leaves the corners: an inset shadow is clipped to its own rounded box, so anything outside the
radius receives nothing, and a full-screen element with `border-radius` has four corner slivers with
no effect on them. Keep the radius modest - at `90dp` the deepest uncovered point is about 26dp in
from the corner, which nobody sees during a flash. A permanent layer wants a smaller radius still, or
none.

Measure rather than trusting any of it. The four edge midpoints should read about the same:

```python
im = Image.open("out/edge.png").convert("RGB")
g = lambda x, y: im.getpixel((x, y))[0]
print(g(960, 3), g(960, 1076), g(3, 540), g(1916, 540))
```

## 4. Position the body, or 100% is not the screen

```css
body { position: relative; width: 100%; height: 100%; }
```

An absolutely positioned element resolves against its nearest **positioned** ancestor, and a body left
at `position: static` is not one, so the layer sizes itself against the initial containing block
instead. That block is not always the same box as the laid-out body in engine, and when it is larger
the effect is anchored top left and runs off the bottom right - at exactly the amount the two boxes
differ by, which is a hard thing to recognise as a containing-block problem.

`uipreview` will not show it. The two boxes agree there.

## 5. States cross-fade, events animate

**`box-shadow` is not a property RmlUi can interpolate.** Naming it in a `transition` or in a
`@keyframes` pair warns and does not blend. So a layer's shadow is fixed at author time and the only
thing that ever moves is its `opacity`.

That splits the problem in two, and the split is worth keeping even where you could cheat.

**A state** - burning, poisoned, low health, hunted - is one layer per state, all four corners of the
same box, cross-faded:

```css
#burning { box-shadow: 0dp 0dp 55dp 55dp #f2551b8f inset; transition: opacity 0.45s cubic-out; }
#frozen  { box-shadow: 0dp 0dp 55dp 55dp #3fa9f58f inset; transition: opacity 0.45s cubic-out; }

body.burning #burning { opacity: 1; }
body.frozen  #frozen  { opacity: 1; }
```

Two layers rather than one that changes colour, and a fade rather than a swap, because a state that
snaps on reads as a bug. Give a continuous one a slow breath so it does not go dead on screen:

```css
body.burning #burning { animation: 1.1s sine-in-out infinite breathe; }
@keyframes breathe { from { opacity: 0.55; } 50% { opacity: 1; } to { opacity: 0.55; } }
```

**An event** - a hit, a level-up, a parry - is its own layer with a bigger spread, so it reaches
further in and reads over whatever state is already showing:

```css
#hit { box-shadow: 0dp 0dp 55dp 100dp #ff1d3d inset; opacity: 0; }

body.hit-a #hit { animation: 0.42s cubic-out hit-a; }
body.hit-b #hit { animation: 0.42s cubic-out hit-b; }

@keyframes hit-a { from { opacity: 0; } 12% { opacity: 0.75; } to { opacity: 0; } }
@keyframes hit-b { from { opacity: 0; } 12% { opacity: 0.75; } to { opacity: 0; } }
```

The two identical keyframe blocks are not a mistake. **RCSS restarts an animation only when the
`animation` property changes**, so a single class set twice in a row fires once and the second hit
draws nothing. C++ alternates between the pair:

```cpp
void UMyHudModel::Hit()
{
	const bool bWasA = bHitA;
	bHitA = !bWasA;
	bHitB = bWasA;
}
```

Both flags must go off again once the animation has played, or the layer holds its last frame and the
state layer underneath never comes back. A short countdown in the widget is enough:

```cpp
if (HitHoldRemaining > 0.f)
{
	HitHoldRemaining -= DeltaSeconds;
	if (HitHoldRemaining <= 0.f)
	{
		Model->ClearHit();
	}
}
```

Rise time is what sells an impact. `12%` of a 0.42s animation is 50ms to full and 370ms to fall - the
asymmetry is the whole effect, and an even in-and-out reads as a pulse rather than a hit.

## 6. Four properties that are not optional

```css
.edge {
	z-index: -1;
	opacity: 0;
	pointer-events: none;
	focus: none;
}
```

`z-index: -1` puts every layer behind the readouts, so a hit flash does not wash out the numbers it
is telling the player to look at. `pointer-events: none` and `focus: none` keep a full-screen element
from eating every click and becoming a focus target - on a HUD, `UFolioWidget::bInteractive = false`
already covers this, but these layers turn up in pause menus and inventories too.

`opacity: 0` on the shared class means a layer that is never switched on costs a state change and
nothing else.

There is a fifth, and it goes on the elements rather than the class:

```html
<div id="hit" class="edge" folio-screen-effect/>
```

`folio-screen-effect` is what lets a player turn all of this off. It is three problems in one - a
coloured wash over the HUD is a legibility problem, the breathing overlay is a motion problem, and the
hit flash is a photosensitivity problem - and whoever needs one of them gone needs all of them gone.
Folio cannot tell a vignette from a panel, so mark them. See
**[accessibility](../accessibility.md#screen-effects)**.

## 7. Ordering

Later siblings paint over earlier ones, and every layer here is absolutely positioned, so document
order is the stack:

```html
<div id="base" class="edge"/>      <!-- always on, the shape of the screen -->
<div id="burning" class="edge"/>   <!-- states -->
<div id="frozen" class="edge"/>
<div id="hit" class="edge"/>       <!-- events, last, so they read over everything -->
```

A permanent dark `#base` at low alpha is worth having even in a game with no status effects. It is
what makes the coloured layers read as the screen changing colour rather than as a rectangle
appearing, because the corners were already dark.

## 8. Cost

**`box-shadow` is a layer effect, and every visible one is an offscreen pass** at the size of its
element. These elements are the whole screen, so this is the one place in a document where the stack
depth is worth thinking about. `stat Folio` reports it under **Resolve layers**.

Authoring one layer per state and one per event is still right - it is what makes the cross-fade
possible at all - but keep the number that can be *on at once* small. A state machine where only one
hazard can apply, plus a base and an event layer, is three. Ten status effects each with their own
layer, all able to stack, is not.

Animate `opacity` and nothing else. It is what section 5 already forces, and animating the geometry
instead - `border-radius`, the insets, the shadow's own numbers - rebuilds the blurred layer every
frame rather than reusing it.

## Where to go next

- **[A helmet HUD that responds to the player](visor_hud.md)** — the rest of a HUD around this: anchoring, instruments out of gradients, and motion that trails the camera.
- **[Animation](../animation.md)** — transitions, keyframes, and what can and cannot be interpolated.
- **[UI_CONVENTIONS.md](../../UI_CONVENTIONS.md)** — the full property list. Read it before inventing one.

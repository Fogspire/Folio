# The coloured screen edge

Standing in fire and the corners of the screen glow orange. Something bites you and the whole frame pulses red for a tenth of a second. Health drops under a quarter and a dark red haze sits at the edge until you heal. Every game does some version of this, and it is the cheapest legibility you can buy: it is readable in peripheral vision, it never covers what the player is aiming at, and it needs no art.

**It is one property.** `box-shadow` with the `inset` keyword paints a soft falloff inward from the element's own border, and an element that is the whole view is a vignette.

```css
#hazard {
	position: absolute;
	left: 0dp; top: 0dp; width: 100%; height: 100%;
	border-radius: 90dp;
	box-shadow: 0dp 0dp 55dp 55dp #f2551b8f inset;
}
```

```bash
./bin/win64/uipreview.exe examples/screen_edge_baker.rml --root . --screenshot out/edge.png
```

Two documents ship, and section 8 is why: `screen_edge_baker.rml` is the effect written out as `box-shadow`, and `screen_edge.rml` is the same picture drawn from textures baked out of it. Author in the first; ship the second.

---

## 1. Why not a gradient

The obvious first attempt is `decorator: radial-gradient(...)` on a full-screen element. It works, and it is wrong for this, because a radial gradient is an **ellipse**. On a 16:9 view the falloff reaches much further in at the top and bottom than at the sides, and the result reads as a lens rather than as the edge of a screen.

The second attempt is a stack of concentric frames with `border-width` and increasing alpha, faking a ramp. Do not do this. It takes eight elements to stop looking like stripes, it still has visible banding on a flat background, and it is strictly worse than the one property that already exists.

`box-shadow ... inset` follows the **rounded rectangle**, which is the shape of the screen, and its falloff is a real blur.

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

Blur and spread compound: the effect reaches roughly `spread + blur` inward, and the first `spread` of that is at full strength. A hit flash wants a large spread so it is unmissable; a standing hazard wants a large blur and a small spread so it stays out of the way.

## 3. Two limits on a full-screen one

Both of these are specific to an element the size of the view, and both fail by drawing the effect in the wrong place rather than by warning.

**Spread must be non-zero.** `0dp 0dp 60dp 0dp <colour> inset` renders nothing at all. Blur alone does not produce a shadow; something has to be there for it to soften.

**Blur must stay small.** Past about 70dp the whole effect displaces toward the bottom right, and by 120dp the top and left edges have lost it entirely while the bottom and right still have it. Measured on a 1920x1080 view, one element, no transform:

| blur | top | bottom | left | right |
|---|---|---|---|---|
| 30dp | 122 | 122 | 64 | 122 |
| 70dp | 37 | 62 | 10 | 62 |
| 120dp | 0 | 35 | 0 | 35 |
| 200dp | 0 | 20 | 0 | 20 |

So **reach comes from spread, and blur only softens the inner edge**. `55dp` blur with `55dp` spread covers 110dp with a soft inside; `120dp` blur with `20dp` spread covers nothing on two of the four edges. The remaining left-edge deficit in the first row is the outermost two or three pixels only, and is not visible.

Do not try to fix the corners by hanging the element off the view either. An inset shadow on an element larger than the viewport lands misplaced the same way, pulled toward whichever corner it overhangs - which looks exactly like the HUD being anchored top-left and running off the bottom right.

That leaves the corners: an inset shadow is clipped to its own rounded box, so anything outside the radius receives nothing, and a full-screen element with `border-radius` has four corner slivers with no effect on them. Keep the radius modest - at `90dp` the deepest uncovered point is about 26dp in from the corner, which nobody sees during a flash. A permanent layer wants a smaller radius still, or none.

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

An absolutely positioned element resolves against its nearest **positioned** ancestor, and a body left at `position: static` is not one, so the layer sizes itself against the initial containing block instead. That block is not always the same box as the laid-out body in engine, and when it is larger the effect is anchored top left and runs off the bottom right - at exactly the amount the two boxes differ by, which is a hard thing to recognise as a containing-block problem.

`uipreview` will not show it. The two boxes agree there.

## 5. States cross-fade, events animate

**`box-shadow` is not a property RmlUi can interpolate.** Naming it in a `transition` or in a `@keyframes` pair warns and does not blend. So a layer's shadow is fixed at author time and the only thing that ever moves is its `opacity`.

That splits the problem in two, and the split is worth keeping even where you could cheat.

**A state** - burning, poisoned, low health, hunted - is one layer per state, all four corners of the same box, cross-faded:

```css
#burning { box-shadow: 0dp 0dp 55dp 55dp #f2551b8f inset; transition: opacity 0.45s cubic-out; }
#frozen  { box-shadow: 0dp 0dp 55dp 55dp #3fa9f58f inset; transition: opacity 0.45s cubic-out; }

body.burning #burning { opacity: 1; }
body.frozen  #frozen  { opacity: 1; }
```

Two layers rather than one that changes colour, and a fade rather than a swap, because a state that snaps on reads as a bug. Give a continuous one a slow breath so it does not go dead on screen:

```css
body.burning #burning { animation: 1.1s sine-in-out infinite breathe; }
@keyframes breathe { from { opacity: 0.55; } 50% { opacity: 1; } to { opacity: 0.55; } }
```

**An event** - a hit, a level-up, a parry - is its own layer with a bigger spread, so it reaches further in and reads over whatever state is already showing:

```css
#hit { box-shadow: 0dp 0dp 55dp 100dp #ff1d3d inset; opacity: 0; }

body.hit-a #hit { animation: 0.42s cubic-out hit-a; }
body.hit-b #hit { animation: 0.42s cubic-out hit-b; }

@keyframes hit-a { from { opacity: 0; } 12% { opacity: 0.75; } to { opacity: 0; } }
@keyframes hit-b { from { opacity: 0; } 12% { opacity: 0.75; } to { opacity: 0; } }
```

The two identical keyframe blocks are not a mistake. **RCSS restarts an animation only when the `animation` property changes**, so a single class set twice in a row fires once and the second hit draws nothing. C++ alternates between the pair:

```cpp
void UMyHudModel::Hit()
{
	const bool bWasA = bHitA;
	bHitA = !bWasA;
	bHitB = bWasA;
}
```

Both flags must go off again once the animation has played, or the layer holds its last frame and the state layer underneath never comes back. A short countdown in the widget is enough:

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

Rise time is what sells an impact. `12%` of a 0.42s animation is 50ms to full and 370ms to fall - the asymmetry is the whole effect, and an even in-and-out reads as a pulse rather than a hit.

## 6. Four properties that are not optional

```css
.edge {
	z-index: -1;
	opacity: 0;
	pointer-events: none;
	focus: none;
}
```

`z-index: -1` puts every layer behind the readouts, so a hit flash does not wash out the numbers it is telling the player to look at. `pointer-events: none` and `focus: none` keep a full-screen element from eating every click and becoming a focus target - on a HUD, `UFolioWidget::bInteractive = false` already covers this, but these layers turn up in pause menus and inventories too.

`opacity: 0` on the shared class means a layer that is never switched on costs a state change and nothing else.

There is a fifth, and it goes on the elements rather than the class:

```html
<div id="hit" class="edge" folio-screen-effect/>
```

`folio-screen-effect` is what lets a player turn all of this off. It is three problems in one - a coloured wash over the HUD is a legibility problem, the breathing overlay is a motion problem, and the hit flash is a photosensitivity problem - and whoever needs one of them gone needs all of them gone. Folio cannot tell a vignette from a panel, so mark them. See **[accessibility](../accessibility.md#screen-effects)**.

## 7. Ordering

Later siblings paint over earlier ones, and every layer here is absolutely positioned, so document order is the stack:

```html
<div id="base" class="edge"/>      <!-- always on, the shape of the screen -->
<div id="burning" class="edge"/>   <!-- states -->
<div id="frozen" class="edge"/>
<div id="hit" class="edge"/>       <!-- events, last, so they read over everything -->
```

A permanent dark `#base` at low alpha is worth having even in a game with no status effects. It is what makes the coloured layers read as the screen changing colour rather than as a rectangle appearing, because the corners were already dark.

## 8. Cost, and baking it away

**`box-shadow` is a layer effect: every visible one is an offscreen pass at the size of its element.** These elements are the whole screen, so each is a full-screen render target - allocated, cleared, drawn and composited. And because a document with a running animation never idles, that is not once, it is every frame. `stat Folio` reports it under **Resolve layers**; the layer memory shows up as **Layer targets**.

It is the most expensive thing a document can do, and it looks exactly the same as the cheap version, which is why `uipreview` warns rather than leaving it to a profiler:

```
warning: [W-PERF-FULLSCREENLAYER] #burning: box-shadow on an element covering 100% of the
viewport costs a full-screen render target, and it is animating, so that is one every frame.
```

Anything covering at least 75% of the viewport with `box-shadow`, `filter`, `backdrop-filter` or `mask-image` gets one, and `--strict` fails on it.

### Bake it, in the engine

Nothing about the wash changes at runtime, so it should not be recomputed at runtime. A bake renders one element alone, on nothing, cropped to its border box, and writes it as a PNG with straight alpha. From the editor console:

```
folio.Bake /folio/examples/screen_edge_baker.rml #base examples/art/edge_base.png 1920 1080
folio.Bake /folio/examples/screen_edge_baker.rml #burning examples/art/edge_burning.png 1920 1080
folio.Bake /folio/examples/screen_edge_baker.rml #frozen examples/art/edge_frozen.png 1920 1080
folio.Bake /folio/examples/screen_edge_baker.rml #hit examples/art/edge_hit.png 1920 1080
```

**The engine, not `uipreview`.** The two renderers do not draw `box-shadow` the same - this shadow peaks at alpha 16 in the preview and 70 in the engine - so a texture baked there is faint and the wrong shape in the game. `uipreview --bake` refuses a layer effect and prints the line above instead.

The element is baked at full opacity whatever the document rests at, so the `opacity: 0` section 5 put on every state layer does not bake an empty texture. A *running animation* is the exception: it drives the property itself and cancelling one is not reliable, which is why the baker carries no state classes on its `body`. Dress it in `hit-a` and the hit layer bakes as nothing at all.

Then draw the textures, and delete the property and the radius that made them:

```css
.edge {
	position: absolute;
	left: 0dp; top: 0dp; width: 100%; height: 100%;
	z-index: -1;
	opacity: 0;
	pointer-events: none;
	focus: none;
}

#base    { decorator: image(/folio/examples/art/edge_base.png fill);    opacity: 1; }
#burning { decorator: image(/folio/examples/art/edge_burning.png fill); transition: opacity 0.45s cubic-out; }
```

**The corners are in the art now.** No `border-radius`, no `box-shadow` - the shape stopped being something the renderer works out per frame, which is the whole point.

Every state and every event still works unchanged, because none of it was ever the shadow: sections 5 and 7 are about `opacity`, and `opacity` is free. Six quads replace four full-screen passes.

In VSCode the warning carries the `folio.Bake` line for the element it flagged, with a button to copy it.

### Keep both documents

The baker is not scaffolding to delete. It is the only readable description of the effect, and it is what you edit when the orange is too strong or the falloff too tight - change it, re-bake, done. Deleting it leaves four PNGs nobody can adjust.

Mark it so the warning knows the difference:

```html
<body class="burning hit-a" folio-bake-source>
```

That says the document exists to be baked - its cost is paid once, by a tool - and the full-screen layer warning skips it. It is the only exemption there is, and it is a statement about the document rather than a way to silence the check.

### When not to bake

A wash is the easy case: it is soft, so stretching one texture over any viewport is invisible. Bake at the size it will be drawn at, and be more careful with anything carrying an edge - `fill` stretches, and a hard line stretched off its pixel grid will show.

An effect whose *shape* changes at runtime cannot be baked at all. A `backdrop-filter` blurring live content behind a panel is the real example: it is expensive for a reason, and the reason is that it reads the frame underneath.

## Where to go next

- **[A helmet HUD that responds to the player](visor_hud.md)**: the rest of a HUD around this: anchoring, instruments out of gradients, and motion that trails the camera.
- **[Animation](../animation.md)**: transitions, keyframes, and what can and cannot be interpolated.
- **[Performance and profiling](../performance_profiling.md)**: `stat Folio`, what the layer passes cost, and `--bake` in full.
- **[UI_CONVENTIONS.md](../../UI_CONVENTIONS.md)**: the full property list. Read it before inventing one.

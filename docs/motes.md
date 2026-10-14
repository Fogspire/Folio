# Drifting motes

Slow specks falling behind a menu, at three depths. Atmosphere, and nothing else: nothing in the field is interactive, carries meaning, or costs anything a still background would not.

```html
<link type="text/template" href="/folio/lib/lib.rml"/>
```
```html
<div class="motes">
	<div class="motes-layer motes-far">
		<div class="motes-scroll"><div class="motes-tile"/><div class="motes-tile"/></div>
	</div>
	<div class="motes-layer motes-mid">
		<div class="motes-scroll"><div class="motes-tile"/><div class="motes-tile"/></div>
	</div>
	<div class="motes-layer motes-near">
		<div class="motes-scroll"><div class="motes-tile"/><div class="motes-tile"/></div>
	</div>
</div>
```

`examples/motes.rml` is the whole thing over a menu.

## There are no particles

Each depth is one tile drawn twice, stacked, scrolled by a looping `translateY` on their shared parent. The tiles are seamless top to bottom, and the scroll travels exactly one tile, so the second copy arrives where the first started and the loop has no frame you can point at. Three depths at unrelated speeds read as parallax; a slow horizontal sway on top of it, at periods sharing no factor with the fall, stops the field ever appearing to pulse.

The whole thing is **six quads and one transform each**. Nothing is spawned, nothing is simulated, and the cost does not change with how many motes are on screen - that is a property of the art.

A real particle system is the wrong tool here twice over. RCSS has no seam for one, and the effect does not want one: motes that never collide, never react and never burst are a scrolling texture wearing a costume.

## Where it goes

`.motes` is absolutely positioned and fills its parent, so **the parent has to be the thing it should cover**, with `position: relative` (or absolute) and `overflow: hidden` on it. Usually that is the document body. Anything after it in document order draws over the top; `pointer-events: none` keeps it out of hit testing.

A wash between the field and the text is worth adding wherever the two overlap. A `horizontal-gradient` behind a menu column is enough, and it is what the example does. Prefer a linear gradient to a radial one: against a near-black background a radial gradient bands visibly into rings.

## Tuning it

Everything is a rule to restate in the game's own stylesheet, loaded after the library.

**Colour and strength** are `image-color` on the tiles, and they are the whole look:

```css
.motes-far  .motes-tile { image-color: #c8d0ff4d; }
.motes-mid  .motes-tile { image-color: #dce4ff66; }
.motes-near .motes-tile { image-color: #f0c67440; }
```

The tiles are white, so any colour is reachable this way. Raise the alpha before the brightness - a mote that reads as a light is one that stays faint.

**Speed** is the animation duration. Slower is almost always better; the shipped near layer takes 19 seconds to cross the screen and still reads as too fast on a large display.

```css
.motes-near .motes-scroll { animation: 30s linear-in infinite motes-fall; }
```

The easing has to be `linear-in`. Any other curve accelerates and decelerates across the loop, and the seam becomes the one moment the field changes speed.

**Depth** is `filter: blur(3dp)` on a layer if the baked softness is not enough. It costs a full-screen render target per blurred layer, so bake it into the art instead where you can - the generator's `falloff` parameter is exactly that dial.

**Art** is three PNGs baked by `Tools/uiart/make_motes.py`, which takes count, radius, alpha and falloff per layer and stamps every dot at its wrapped neighbours so the tile is seamless. It is deterministic: same seed, same bytes.

```bash
python Tools/uiart/make_motes.py
```

Point the decorator somewhere else for hand-drawn art. It must be a file rather than a sprite, it must tile vertically, and it should carry the box's aspect ratio - 1920x1200dp, so 960x600 - or every mote draws as an ellipse.

## The two decisions behind it

**A percentage in an animated `transform` never moves.** It is resolved once, when the animation starts, which on a document's initial style is before the first layout - so it resolves against a zero box and both ends of the keyframe collapse to zero. Nothing is logged. The fall is therefore a length, `1200dp`, and the tile height repeats it; change one and change the other. See **[animation](animation.md)**.

Two stacked copies of height H cover any viewport up to H tall at every scroll offset, and under Unreal's default DPI curve a landscape viewport is about 1080dp tall whatever the resolution. A game that turns the dp follow down far enough to exceed 1200dp restates the height, the `top` on the first tile, and the keyframe.

**`image(src repeat)` would be the obvious way to tile, and `fill` is the better one.** A repeating tile is its texture's size in dp, so a 960dp tile across a 1920dp screen draws the same motes twice, side by side - and a mote field is exactly the kind of thing where a mirrored half is visible once you have seen it. Stretching one tile over the box never repeats, costs the same, and only asks that the art carry the box's ratio.

## Accessibility

Reduced motion stops it. `@media (theme: reduced-motion)` in `folio.rcss` turns off every animation in the document, and the field settles into a still starfield rather than vanishing - the atmosphere survives and the movement does not.

High contrast removes it outright, in the theme, because nothing is carried by the field and a moving texture behind text is exactly what high contrast exists to take away.

Neither needs anything from the document.

## Where to go next

- **[Animation](animation.md)**: the percentage trap in full, and what else is cheap to animate.
- **[Images and sprites](images.md)**: fit modes, and when a repeating tile beats a stretched one.
- **[Components and themes](components_and_themes.md)**, where a tint belongs, and how overriding works.
- **[Performance and profiling](performance_profiling.md)**: what a screen costs once it is drawing this.

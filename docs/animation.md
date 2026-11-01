# Animation

Hover states, pulses, entries and exits, written in the stylesheet with no animation code.
![A header cutting in with a keyframe animation](https://github.com/user-attachments/assets/1a730508-cbdb-4a50-b6aa-4a2e3fa4ed5d)

`transition` reacts to a state change; `@keyframes` with `animation` drives itself.

```css
.btn        { transition: background-color 0.12s cubic-out, transform 0.12s cubic-out; }
.btn:hover  { background-color: #2b2b36; }
.btn:active { transform: scale(0.97); }
```

`transition` fires **only when a class or pseudo-class changes**. For continuous motion use keyframes.

```css
@keyframes pulse {
	from { opacity: 0.4; }
	50%  { opacity: 1.0; }
	to   { opacity: 0.4; }
}

.alert { animation: 1.2s cubic-in-out infinite pulse; }
```

## Traps

Each parses cleanly and then misbehaves, mostly with no warning.

**Times are seconds, and `ms` does not exist.** `transition: opacity 120ms` reads `120` as a *reverse adjustment factor* and the transition ends up with no duration. Write `0.12s`.

**The easing names are RmlUi's, not CSS's.** No `ease`, `ease-in`, `ease-out`, `ease-in-out`, `linear`, `step-*` or `cubic-bezier()`. A misspelled easing name is read as a property name, which silently produces a transition that never fires.

The set is `<family>-in`, `<family>-out` or `<family>-in-out` for each of:

`back` `bounce` `circular` `cubic` `elastic` `exponential` `linear` `quadratic` `quartic` `quintic` `sine`

The everyday ones: **`cubic-out`** for something arriving, **`cubic-in`** for something leaving, and `cubic-in-out` for something moving between two places.

**3D transforms work.** `perspective()` (or the `perspective` property on the parent) with `rotateX`, `rotateY` and `translateZ` tilts an element like a card in space: `transform: perspective(400dp) rotateY(-30deg)`. The renderer projects a tilted element point by point on the CPU, so a large one tilted steeply costs more; a panel or a card is cheap.

**To animate a tilt, put the perspective on the parent.** A `perspective()` inside a transform that a `transition` or `@keyframes` interpolates makes RmlUi interpolate the whole matrix decomposed, which flings the element across the screen mid-animation or leaves it invisible. Give the parent `perspective: 1800dp` (and `perspective-origin` if the vanishing point should not be its centre) and animate only `rotateY(...)` and friends on the child. A static `perspective()` that never animates is fine. The parent's perspective reaches its direct children only.

**`opacity` on an element with overlapping children fades wrong, and the middle of the fade is the only place it shows.** RCSS inherits `opacity`, unlike CSS, and multiplies it into premultiplied vertex colours rather than compositing the subtree as one image. Every descendant is drawn on its own, overlapping quads accumulate instead of blending, and a light panel over a dark shadow blows out to white halfway through. Both ends look right: the symptom is a flash partway through an entry animation.

```css
/* A card that is a plate over a shadow. This flashes white as it arrives. */
@keyframes card-in {
	from { transform: translateY(28dp) scale(0.94); opacity: 0; }
	to   { transform: translateY(0dp) scale(1); opacity: 1; }
}

/* Move it instead, and let the elements keep their own alpha. */
@keyframes card-in {
	from { transform: translateY(28dp) scale(0.94); }
	to   { transform: translateY(0dp) scale(1); }
}
```

Put opacity on a single leaf element, or animate `transform` alone. Fading a group *as one image* means compositing it with a `filter`, which costs a render target.

A property carrying both a transition and an animation is a related trap: a transition is an animation here, so one of the two is dropped. The log says `Could not animate property 'opacity' on element` and names neither rule.

**A percentage inside an animated `transform` resolves once, when the animation starts, and on a document's initial style that is before the first layout.** The element has no box yet, so `translateY(100%)` resolves against a height of zero, both ends of the keyframe become `translateY(0px)`, and the animation runs forever without moving anything. Nothing is logged, so it reads as the animation not being applied.

```css
/* Never moves. */
@keyframes scroll { from { transform: translateY(0%); } to { transform: translateY(100%); } }

/* Moves. */
@keyframes scroll { from { transform: translateY(0dp); } to { transform: translateY(1200dp); } }
```

Write the distance in `dp`. A percentage is only safe on an animation that starts after the first layout (one driven by a class the game adds), and it is not worth the distinction. `transition` is unaffected: it starts from a state change, by which point the element has a box.

## Cheap to animate

**Animate `transform`, `opacity`, `color`, `background-color`, `border-color` and `filter`.** None of them triggers reflow.

**Never animate `width`, `height`, `margin`, `padding`, `top`/`right`/`bottom`/`left`, `font-size` or `line-height`.** Each triggers layout, every frame, for every animating element. A HUD doing this is measurably slower than the UMG it replaced.

Substitutions:

| Instead of | Use |
|---|---|
| animating `width` | `transform: scaleX()` on an element with a fixed box |
| animating `left` | `transform: translateX()` |
| animating `height` to reveal | `transform: scaleY()`, or `max-height` on a fixed box, or opacity |

A health bar's fill is the exception: it wants `width`, because the bar has to end where the value says rather than being squashed. One bar is fine. Thirty animating nameplates is thirty layouts a frame; see **[markers](markers.md)**.

**`transition: all 0.12s` is legal and wrong.** It transitions the layout properties too. Name the properties.

## Animating something out

A removed element cannot animate. Folio's close and drop behaviours set a class and *wait*:

```css
@keyframes suck { to { transform: translateY(520dp) scale(0.04); opacity: 0; } }
.window.folio-closing { animation: 0.5s cubic-in suck; }
```

`folio-closing` goes on anything on its way out, by a `folio-close` click **or** by a `folio-drop`, and the removal waits for the animation to finish. Without an animation on the class, the element hides at once. See **[drag, drop and resize](drag_and_resize.md)**.

For anything else, do it by hand: add a class, listen for the animation to end, then remove.

## Driving animation from game state

A transition fires on a class change, and `data-class-*` changes a class from a bound value. No animation code:

```html
<div class="bar-fill"
	data-style-width="Health + '%'"
	data-class-critical="Health &lt; 20"></div>
```

```css
.bar-fill          { transition: width 0.15s cubic-out, background-color 0.3s cubic-out; }
.bar-fill.critical { background-color: #e06c75; }
```

See **[data binding](data_binding.md)**.

## Sequencing

With no timeline and no JavaScript, sequences are built from delays:

```css
.row-1 { animation: 0.3s cubic-out 0.00s enter; }
.row-2 { animation: 0.3s cubic-out 0.06s enter; }
.row-3 { animation: 0.3s cubic-out 0.12s enter; }
```

For a list, generate the classes or use `data-for` with an index and `data-style-*`. For anything complex, set classes from C++ on a timer.

## Checking it

`uipreview` renders live and re-runs on save. `--frames` controls how many frames settle before a capture, so the middle of an entry animation screenshots reproducibly.

**In [Folio Studio](studio.md), open the Timeline** (`Ctrl+Shift+T`). Play it from the first frame, or drag to any moment, to see an entry animation that otherwise finishes before you look.

On the command line, for a diff or a bug report:

```bash
./bin/win64/uipreview.exe UI/screens/menu.rml --root . --filmstrip out/ --filmstrip-frames 30 --dt 33.3
```

One PNG per frame, plus a `filmstrip.json` naming them and the step between them. Time is simulated and fixed-step, so the same command always produces the same frames: `--dt` is the sampling interval, not the game's frame rate.

A document that never idles is usually one element still transitioning something invisible. Find it with `folio.Invalidation.Trace`; see **[performance](performance_profiling.md)**.

## What transitions cannot do

A transition runs a fixed curve to completion with no notion of current velocity, so it cannot follow a signal that never settles, such as a HUD trailing a moving camera.

That needs a spring, which is a component rather than a stylesheet feature. See **[a HUD that responds to the camera](hud_motion.md)**.

## Where to go next

- **[A HUD that responds to the camera](hud_motion.md)**: springs, for motion a curve cannot describe.
- **[Flipbooks](flipbook.md)**: frame-by-frame animation from a sheet.
- **[Drag, drop and resize](drag_and_resize.md)**: `folio-closing` and animated removal.
- **[Data binding](data_binding.md)**: driving classes from gameplay.
- **[Performance and profiling](performance_profiling.md)**: when the animation is the cost.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §4: the full reference.

# Animation

Two mechanisms. `transition` reacts to a state change; `@keyframes` with `animation` drives itself.

```css
.btn        { transition: background-color 0.12s cubic-out, transform 0.12s cubic-out; }
.btn:hover  { background-color: #2b2b36; }
.btn:active { transform: scale(0.97); }
```

`transition` fires **only when a class or pseudo-class changes**. For continuous or self-driving
motion you want keyframes.

```css
@keyframes pulse {
	from { opacity: 0.4; }
	50%  { opacity: 1.0; }
	to   { opacity: 0.4; }
}

.alert { animation: 1.2s cubic-in-out infinite pulse; }
```

## Four things that will bite you

These are not style advice. Each produces something that parses cleanly and then misbehaves, mostly
with no warning at all.

**Times are seconds, and `ms` does not exist.** `transition: opacity 120ms` reads `120` as a *reverse
adjustment factor* and the transition ends up with no duration. Write `0.12s`.

**The easing names are RmlUi's, not CSS's.** There is no `ease`, `ease-in`, `ease-out`,
`ease-in-out`, `linear`, `step-*` or `cubic-bezier()`. A misspelled easing name is read as a property
name instead, which silently produces a transition that never fires.

The set is `<family>-in`, `<family>-out` or `<family>-in-out` for each of:

`back` `bounce` `circular` `cubic` `elastic` `exponential` `linear` `quadratic` `quartic` `quintic`
`sine`

So the everyday ones are **`cubic-out`** for something arriving, **`cubic-in`** for something leaving,
and `cubic-in-out` for something moving between two places.

**3D transforms do not map onto the renderer.** No `rotateX`, `rotateY` or `perspective`. 2D only:
`translate`, `scale`, `rotate`, `skew`.

**`opacity` on an element with overlapping children fades wrong, and the middle of the fade is the
only place it shows.** RCSS inherits `opacity`, unlike CSS, and multiplies it into premultiplied
vertex colours rather than compositing the subtree as one image. Every descendant is drawn on its
own, overlapping quads accumulate instead of blending, and a light panel over a dark shadow blows
out to white halfway through. Both ends look right, which is what makes it hard to place: the
symptom is a flash partway through an entry animation, not a wrong final state.

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

Put opacity on a single leaf element, or animate `transform` alone. Fading a group *as one image*
means compositing it, which means a `filter`, which costs a render target - a deliberate choice
rather than something to reach for.

A property carrying both a transition and an animation is the related trap: a transition is an
animation here, so one of the two is dropped. The log says `Could not animate property 'opacity' on
element` and names neither rule.

## What is cheap

**Animate `transform`, `opacity`, `color`, `background-color`, `border-color` and `filter`.** None of
them triggers reflow.

**Never animate `width`, `height`, `margin`, `padding`, `top`/`right`/`bottom`/`left`, `font-size` or
`line-height`.** Each triggers layout, every frame, for every animating element. A HUD doing this will
be measurably slower than the UMG implementation it replaced - which is the one outcome Folio exists
to avoid.

The substitutions are always available:

| Instead of | Use |
|---|---|
| animating `width` | `transform: scaleX()` on an element with a fixed box |
| animating `left` | `transform: translateX()` |
| animating `height` to reveal | `transform: scaleY()`, or `max-height` on a fixed box, or opacity |

A health bar is the exception worth knowing: its fill really does want `width`, because the bar has to
end where the value says rather than being squashed. One bar is fine. Thirty animating nameplates is
thirty layouts a frame - see **[markers](markers.md)**.

**`transition: all 0.12s` is legal and wrong.** It transitions every animatable property including
the layout ones. Name the properties.

## Animating something out

An element that is removed cannot animate - it is gone. Folio's close and drop behaviours solve that
by setting a class and *waiting*:

```css
@keyframes suck { to { transform: translateY(520dp) scale(0.04); opacity: 0; } }
.window.folio-closing { animation: 0.5s cubic-in suck; }
```

`folio-closing` goes on anything on its way out, by a `folio-close` click **or** by a `folio-drop`,
and the removal waits for the animation to finish. Without an animation on the class, the element
hides at once. See **[drag, drop and resize](drag_and_resize.md)**.

For anything else, the pattern is the same by hand: add a class, listen for the animation to end, then
remove.

## Driving animation from game state

A transition fires on a class change, and `data-class-*` changes a class from a bound value. That is
the whole mechanism - no animation code:

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

There is no timeline and no JavaScript. Sequences are built from delays:

```css
.row-1 { animation: 0.3s cubic-out 0.00s enter; }
.row-2 { animation: 0.3s cubic-out 0.06s enter; }
.row-3 { animation: 0.3s cubic-out 0.12s enter; }
```

Tedious past a handful, so for a list, generate the classes or use `data-for` with an index and
`data-style-*`. For anything genuinely complex, drive it from C++ by setting classes on a timer - the
document is a DOM and game code can reach it.

## Checking it

Animation is the one thing a screenshot cannot show. `uipreview` renders live and watches the files,
so save and it re-runs. `--frames` controls how many frames settle before a capture, which is how you
screenshot the middle of an entry animation reproducibly.

**In VSCode, open the timeline** — the `timeline` button above the preview, or **Folio: Toggle
Timeline**. It renders a frame per step from the document's first and gives you a scrubber and a play
button over them, so an entry animation can be watched, stepped through a frame at a time with the
arrow keys, and stopped on the frame that is wrong. See **[the extension](vscode.md)**.

The same thing on the command line, for a diff or a bug report:

```bash
./bin/win64/uipreview.exe UI/screens/menu.rml --root . --filmstrip out/ --filmstrip-frames 30 --dt 33.3
```

One PNG per frame, plus a `filmstrip.json` naming them and the step between them. Time is simulated
and fixed-step, so the same command always produces the same frames: `--dt` is the sampling interval,
not a claim about the rate the game will run at.

The invalidation trace is how you find an animation you forgot about: a document that never idles is
usually one element still transitioning something invisible. `folio.Invalidation.Trace`, and
**[performance](performance_profiling.md)**.

## What transitions cannot do

A transition fires on a state change and runs a fixed curve to completion. It has no notion of
current velocity, so it cannot express a continuous response to a signal that never settles - a HUD
trailing a moving camera, kicked again before it arrives.

That is a spring, and it is a component rather than a stylesheet feature. See
**[a HUD that responds to the camera](hud_motion.md)**.

## Where to go next

- **[A HUD that responds to the camera](hud_motion.md)** — springs, for motion a curve cannot describe.
- **[Drag, drop and resize](drag_and_resize.md)** — `folio-closing` and animated removal.
- **[Data binding](data_binding.md)** — driving classes from gameplay.
- **[Performance and profiling](performance_profiling.md)** — when the animation is the cost.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §4 — the full reference.

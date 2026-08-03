# Performance and profiling

Folio reports itself through Unreal's ordinary tools. There is no separate profiler to learn: `stat`,
Unreal Insights, CSV captures and LLM all know about it.

## The quick look: `stat Folio`

```
stat Folio
```

An on-screen breakdown of where Folio's frame goes:

| Row | What it covers |
|---|---|
| **Update** | RmlUi's own work: style resolution, layout, data model updates, animations |
| **Render** | Turning the document into Slate draw elements |
| **Compile geometry** | Building a mesh RmlUi will reuse. Should be rare after a screen settles |
| **Draw geometry** | Emitting one compiled mesh. Called a lot; each call should be tiny |
| **Load texture** | Decoding art. Should be zero except when a screen first opens |
| **Resolve layers** | The render-target passes behind `filter`, `box-shadow` and `backdrop-filter` |
| **RT: replay layer** | Replaying one layer's draws on the render thread. Broken down by **RT: layer graph**, **RT: layer buffers** and **RT: layer state** |
| **Draws** | Draw calls this frame |
| **Geometry batches** | Batches this frame |

**What to look at first.** `Draws` and `Geometry batches` climbing is the usual cause of a slow screen,
and it is almost always texture switches — art that should be in one atlas spread across several. That
is a packing problem, not a renderer problem.

**Compile geometry** repeating every frame means something is invalidating geometry that should be
stable. Animating `width`, `height`, `margin` or `padding` does exactly this, because those change
layout. Animate `transform`, `opacity` and colour instead; that is the single biggest authoring
decision affecting Folio's cost.

Also useful, and Slate's rather than ours:

```
stat Slate
```

Its batch count is the honest measure of how well Folio's output is compositing with everything else.

## Unreal Insights

Folio has its own trace channel, so a capture can ask for it without everything else:

```
-trace=default,folio
```

Every scope in `stat Folio` appears in the timeline, including the ones inside the portable core — the
distance field generator names its own scopes through a hook table, so glyph generation shows up as
Folio work rather than as an unexplained gap.

For a CSV capture the category is `Folio`. For memory, `-llm` attributes the glyph atlases and RmlUi's
own allocations to a Folio tag, so they read as Folio rather than as untagged texture memory.

## Idle cost: a document that has not changed does nothing

This is the thing most likely to surprise you, so it is worth understanding rather than trusting.

A naive integration runs RmlUi's update and render every frame for every open document, whether
anything changed or not. Folio does not: it asks RmlUi when the document next needs attention, and
skips the frame entirely until then. A static pause menu costs approximately nothing.

`folio.Invalidation` controls it and is **on by default**.

The signal comes from RmlUi itself, which tracks the cases that are easy to get wrong: a running
`animation` or `transition`, a blinking caret, a held scroll arrow, a smooth scroll. Folio adds the
ones on its own side of the boundary: input, loading a document, a resize, a data model change, a
stylesheet reload.

### When a document seems frozen

The failure mode of this feature is a document that stops updating, and it presents as something small
— the caret stopped blinking, a hover state stuck, a HUD number stopped moving.

**First, prove whether invalidation is the cause:**

```
folio.Invalidation 0
```

If the symptom goes away, the dirty tracking missed a change. If it does not, invalidation is innocent
and the problem is elsewhere.

**Then find out what Folio thinks:**

```
folio.Invalidation.Trace 1
```

Each update logs why it happened and when RmlUi wants the next one:

```
folio invalidation: updated because 'mouse move', next update in never, until input
folio invalidation: updated because 'data model', next update in 0.694s
folio invalidation: updated because 'RmlUi asked', next update in 0.000s
```

Read it like this:

- **`never, until input`** — the document is genuinely static. Correct for a settled menu.
- **`0.000s`** — something is animating, so every frame counts. Correct while a transition runs.
- **a small delay** — a timer, almost always a caret.
- **no lines at all after the first few** — the document is idle. If it should not be, something
  changed it without saying so.

If you change a document from game code in a way Folio cannot see — reaching into the RmlUi DOM
directly rather than through a data model — tell it:

```cpp
Canvas->MarkDirty(TEXT("my own reason"));
```

The reason is what the trace prints, so make it name your code.

### Checking it still works

```bash
./scripts/check_invalidation.sh
```

Runs a real editor against three documents and asserts **both directions**, because a build where
everything idles forever would pass a check that only tested idling:

- a static document updates a handful of times and then stops
- a document with a running animation updates every frame
- a document with a focused, blinking caret updates on the blink timer

Worth running after anything that touches input, data binding or document loading — those are the
paths that raise the dirty flag, and a missed one is a frozen document.

## If a screen is slow

In order of likelihood, and check these before concluding anything structural:

1. **Geometry re-uploaded every frame.** Watch `Compile geometry` in `stat Folio`. Usually caused by
   animating a layout property.
2. **Animation on the wrong properties.** `width`, `height`, `margin`, `padding` trigger reflow.
   `transform`, `opacity` and colour do not.
3. **Poor batching from texture switches.** `Draws` and `Geometry batches` tell you; the fix is
   atlasing more aggressively rather than anything in code.
4. **Layer effects.** `filter`, `backdrop-filter` and `box-shadow` each need an offscreen pass, which
   `Resolve layers` measures. They are not free and a screen using several is paying for each. On a
   full-screen element it is the most expensive thing in the document - see below, and bake it.
5. **A polled data model.** `BindViewModel(..., bPollEveryFrame = true)` re-reads every bound field
   every frame, and it also stops the document from ever idling. A source implementing
   `INotifyFieldValueChanged` — which `UMVVMViewModelBase` does — updates on change instead, which is
   the reason to use one.

## A full-screen layer effect, and baking it away

The single most expensive thing a document can do, and the easiest one to write by accident:

```css
/* A damage vignette. One full-screen render target, allocated, cleared, drawn and composited. */
#scorch {
	position: absolute; left: 0; top: 0; width: 100%; height: 100%;
	border-radius: 90dp;
	box-shadow: 0dp 0dp 70dp 12dp #f2551b66 inset;
}
```

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` are each drawn by rendering the element
into an offscreen target and reading it back. On a full-screen element that target is the size of the
screen, and on a document with a running animation it is resolved **every frame** - because an
animating document never idles. A HUD with three of them spends several milliseconds a frame drawing
a wash that never changes.

**Size is not what a layer costs.** The pass is the cost - a target bound, drawn and composited - and
it is much the same at any size. Two 236x50dp drop shadows on a HUD measured 1.26ms of render thread
between them, heavier than the full-screen vignette beside them. Count them before you measure them.

**The worst place to put one is an element that resizes.** The effect is rendered into a texture
sized to the element, and the texture is thrown away and rebuilt whenever that size changes - so a
`transition: width` regenerates it every frame the transition runs, for a shadow that did not change
shape.

`uipreview` reports all three, and `--strict` gates the first:

| | |
|---|---|
| `W-PERF-RESIZINGLAYER` | A layer effect on an element whose box is transitioned. The expensive one, and the only one that fails a build |
| `I-PERF-FULLSCREENLAYER` | A layer effect covering at least 75% of the viewport |
| `I-PERF-LAYERCOUNT` | How many elements draw one at all |

Only the first is a warning. A vignette or a scrim built the obvious way is a legitimate document,
and a texture is not automatically the cheaper answer - see below. The other two are there to be
read next to `stat Folio`, not to block anything.

### A texture is not free either

Baking moves the cost rather than removing it. A full-screen PNG is decoded, premultiplied and
uploaded **on the game thread, inside `Render`, the first frame it is drawn** - and at 1920x1080 that
is eight megabytes of it. Swapping a full-screen `box-shadow` for a full-screen texture can trade a
steady render-thread cost for a game-thread spike the first time the effect appears, which is worse:
the spike lands exactly when something dramatic is happening on screen.

So bake soft things small. `folio.Bake` takes a downscale as its last argument, which renders at the
size the effect will be drawn at and writes a fraction of it:

```
folio.Bake UI/screens/hud_baker.rml #scorch UI/art/scorch.png 1920 1080 4
```

That is 480x270 - half a megabyte instead of eight, and on a soft wash there is nothing to see.
**Do not bake at a small size instead.** A `70dp` blur is a much larger fraction of a 270dp box than
of a 1080dp one, so the vignette comes out thicker; the render has to happen at the real size and the
downsample after it.

**A document that idles pays none of it.** Folio skips the frame entirely when nothing changed, so
layers on a settled screen are free. One running animation anywhere in the document ends that, and
every layer in it starts costing every frame - which is why a screen effect coming up can look like
it caused a spike that was actually sitting there all along.

### Bake it, in the engine

The effect does not change, so it should be a texture. A bake renders one element alone, on nothing,
cropped to its border box, and writes it as a PNG with straight alpha.

**A layer effect has to be baked in the engine**, from the editor console:

```
folio.Bake UI/screens/hud_baker.rml #scorch UI/art/scorch.png 1920 1080
```

`uipreview --bake` does the same thing and is quicker to reach, but the two renderers do not draw
`box-shadow`, `filter`, `backdrop-filter` or `mask-image` identically - the same inset shadow peaks
at alpha 16 in the preview and 70 in the engine, with a different falloff. A texture baked in the
preview and drawn in the game is faint and the wrong shape, and it reads as bad art rather than as a
renderer difference. `--bake` refuses those four outright and prints the `folio.Bake` line to use
instead; for everything else - gradients, images, colour, borders - the renderers agree and either
will do:

```bash
uipreview UI/screens/panel.rml --root . --bake "#plate=UI/art/plate.png"
```

Then draw the texture and delete the property that made it:

```css
#scorch { decorator: image(/UI/art/scorch.png fill); }
```

Everything a state or an event did still works, because none of it was ever the shadow: `opacity` is
what fades, cross-fades and flashes, and `opacity` is free.

The VSCode preview puts the `folio.Bake` line for the flagged element on the warning, with a button
to copy it. `Folio: Bake Element to PNG` in the palette runs the preview's own bake, for the elements
that do not need the engine.

Anything can be baked, not just a wash - a glow behind a panel, a masked plate, a gradient stack.
The element is rendered at full opacity whatever the document rests at, so a layer that is faded out
until the game turns it on still bakes.

**Keep both documents.** The one that describes the effect is how it gets changed and re-baked, so it
is worth keeping beside the one that draws the result - `examples/screen_edge_baker.rml` and
`examples/screen_edge.rml` are the pair. Mark the source with `folio-bake-source` on its `body` and
the warning leaves it alone, since its cost is paid once, by a tool.

**Put no state classes on a baker's body.** A bake brings its own element up to full opacity, but a
running animation drives the property itself and RmlUi does not reliably release one - so a layer
caught mid-animation can bake as an empty texture. A baker presents every layer plainly and lets each
bake raise its own. Both bakes warn when the element they are given is animating.

**`backdrop-filter` cannot be baked at all.** It blurs what is behind it, so baked alone - on
nothing - the texture is empty. Both bakes refuse it. It is expensive for exactly the reason it
cannot be a texture, and the ways out are all outside RCSS: drop it to a plain scrim, or, if the
scene behind it is frozen while it is up, have the game capture one blurred frame when it opens and
draw that as an ordinary texture. Two stacked full-screen ones - a blurred scrim under a blurred
modal - is paying twice for a frame nobody is looking past.

Baking is a resolution decision. The texture is stretched with `fill`, which is invisible on a soft
wash and would not be on anything with an edge; bake at the size it will be drawn at.

## Text is not the expensive part

Worth saying because it is a natural suspicion. Glyphs are size-independent distance fields, so text at
a new size costs no atlas work, and `transform: scale` on text costs nothing at all. Generating a glyph
the first time is real work and shows up under **Load texture** and in the font report:

```
folio.Font.Report
```

Faces, variation tuples, page and glyph counts, cache hits and misses. Misses climbing during play
means glyphs are being generated at runtime; the fix is prewarming the charset, not making text cheaper.

# Performance and profiling

Where a screen's frame time goes, and the fix for each usual cause.

## See what a screen costs

In the editor console, with the screen up:

```
stat Folio
```

Three rows answer most questions:

- **Draws** and **Geometry batches**: climbing means texture switches, art that should share an atlas spread across several.
- **Compile geometry**: should be rare once a screen settles. Every frame means something is invalidating stable geometry.
- **Resolve layers**: the offscreen passes behind `box-shadow`, `filter`, `backdrop-filter` and `mask-image`.

`stat Slate` beside it gives the batch count for the whole UI, which is the honest measure of how well Folio's output composites with everything else.

A document that has not changed is skipped entirely, so a settled menu costs approximately nothing. Every row, Insights, CSV and memory tags are in the [profiling reference](profiling_reference.md).

## If a screen is slow

In order of likelihood:

1. **Geometry re-uploaded every frame.** `Compile geometry` repeats every frame. Usually caused by animating a layout property.
2. **Animation on the wrong properties.** `width`, `height`, `margin` and `padding` trigger reflow. Animate `transform`, `opacity` and colour instead; that is the biggest authoring decision affecting Folio's cost.
3. **Poor batching from texture switches.** `Draws` and `Geometry batches` tell you. The fix is atlasing more aggressively, not code.
4. **Layer effects.** Each needs an offscreen pass, which `Resolve layers` measures. See [what a layer effect costs](#what-a-layer-effect-costs).
5. **A polled data model.** `BindViewModel(..., bPollEveryFrame = true)` re-reads every bound field every frame and stops the document from ever idling. A source implementing `INotifyFieldValueChanged`, which `UMVVMViewModelBase` does, updates on change instead.

Text is rarely the cost. Glyphs are size-independent distance fields, so text at a new size costs no atlas work and `transform: scale` on text costs nothing. Generating a glyph the first time shows up under **Load texture**.

## What a layer effect costs

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` each render the element into an offscreen target and read it back. None of this is visible in a screenshot.

```css
/* A damage vignette, and a card's drop shadow. The second one is the expensive mistake. */
#scorch { position: absolute; left: 0; top: 0; width: 100%; height: 100%;
	box-shadow: 0dp 0dp 70dp 12dp #f2551b66 inset; }

.slot { width: 236dp; box-shadow: -4dp 5dp 0dp #16130fcc; transition: width 0.14s cubic-out; }
```

**Count them; size is not the cost.** The pass is (a target bound, drawn and composited), and it is much the same at any size. Two 236x50dp drop shadows on a HUD measured 1.26 ms of render thread between them, heavier than the full-screen vignette beside them.

**Never put one on an element that resizes.** The effect is rendered into a texture sized to the element and rebuilt whenever that size changes, so a `transition: width` regenerates it every frame of the transition.

**A settled screen pays nothing.** A document that idles skips the frame. One running animation means the document is drawn every frame, but a layer whose contents did not change reuses its picture from a recent frame, so an animation costs its own layers, not every layer beside it. A layer that samples a render target the game updates itself, such as a scene capture, is drawn every frame.

`uipreview` reports layer costs; only `W-PERF-RESIZINGLAYER` is a warning, and `--strict` fails on it. The rest are to read next to `stat Folio`. See the [profiling reference](profiling_reference.md#uipreview-performance-findings).

## Bake a layer effect

An effect that does not change should be a texture. A bake renders one element alone, on nothing, cropped to its border box, and writes a PNG with straight alpha.

1. Bake it in the engine, from the editor console:

   ```
   folio.Bake UI/screens/hud_baker.rml #scorch UI/art/scorch.png 1920 1080
   ```

   A layer effect has to be baked here: the two renderers draw `box-shadow`, `filter`, `backdrop-filter` and `mask-image` differently. For anything else (gradients, images, colour, borders) `uipreview` is quicker and gives the same result:

   ```bash
   uipreview UI/screens/panel.rml --root . --bake "#plate=UI/art/plate.png"
   ```

2. Draw the texture and delete the property that made it:

   ```css
   #scorch { decorator: image(/UI/art/scorch.png fill); }
   ```

   States and events still work: `opacity` is what fades, cross-fades and flashes, and `opacity` is free.

3. Keep the source document beside the one that draws the result, so the effect can be changed and re-baked. `examples/screen_edge_baker.rml` and `examples/screen_edge.rml` are the pair. Put `folio-bake-source` on the source's `body` and the layer warning leaves it alone.

For a soft full-screen wash, pass a downscale as the last argument. It renders at the size the effect is drawn at and writes a fraction of it:

```
folio.Bake UI/screens/hud_baker.rml #scorch UI/art/scorch.png 1920 1080 4
```

That is 480x270: half a megabyte instead of eight, and on a soft wash there is nothing to see.

Anything can be baked: a glow behind a panel, a masked plate, a gradient stack. The element is rendered at full opacity whatever the document rests at, so a layer faded out until the game turns it on still bakes.

## If something is wrong

**A caret stopped blinking, a hover state stuck, or a HUD number stopped moving.** The document idled when it should have updated. Run `folio.Invalidation 0`: if the symptom goes away, the dirty tracking missed a change. Then `folio.Invalidation.Trace 1` logs why each update happened (see [reading the trace](profiling_reference.md#reading-the-invalidation-trace)). If game code changes the document in a way Folio cannot see, such as reaching into the RmlUi DOM directly rather than through a data model, tell it, with a reason, which the trace prints:

```cpp
Canvas->MarkDirty(TEXT("my own reason"));
```

**A document never idles.** The trace says `next update in 0.000s` on every line. Something runs forever: `uipreview` reports it as `I-PERF-NEVERIDLES`, and Folio Studio's **Activity** panel names it. Usually one element still transitioning something invisible, or a polled data model.

**`uipreview --strict` fails with `W-PERF-RESIZINGLAYER`.** A layer effect sits on an element whose box is transitioned. Transition `transform: scaleX()` on a fixed box instead, or bake the effect and animate the texture.

**A baked effect is faint and the wrong shape in game.** It was baked with the preview, where the same inset shadow peaks at alpha 16 instead of the engine's 70, with a different falloff. `uipreview --bake` refuses these with `E-BAKE-NEEDSENGINE` and prints the `folio.Bake` line to use; re-bake with that.

**A bake comes out empty, or `folio bake: '<selector>' is animating`.** A running animation drives the property itself and RmlUi does not reliably release it for the bake. Put no state classes on the baker's `body`: present every layer plainly and let each bake raise its own.

**`folio bake: '<selector>' uses backdrop-filter`.** It blurs what is behind it, so baked alone the texture would be empty, and both bakes refuse it (`E-BAKE-BACKDROP` in `uipreview`). Drop it to a plain scrim or, if the scene behind is frozen while it is up, have the game capture one blurred frame when it opens and draw that as a texture. Two stacked full-screen ones (a blurred scrim under a blurred modal) pay twice.

**A hitch the first frame a baked effect appears, or `folio bake: '<selector>' is <W>x<H>, about <N>MB once loaded`.** A full-screen PNG is decoded, premultiplied and uploaded on the game thread, inside `Render`, the first frame it is drawn: eight megabytes at 1920x1080. Pass a downscale as the last argument of `folio.Bake`.

**A downscaled vignette comes out thicker than the original.** It was baked at a small size instead of downscaled. A `70dp` blur is a much larger fraction of a 270dp box than of a 1080dp one; render at the real size and pass the downscale.

**A baked edge looks soft.** The texture is stretched with `fill`, invisible on a soft wash and visible on anything with an edge. Bake at the size it is drawn at.

**`folio.Font.Report` shows misses climbing during play.** Glyphs are being generated at runtime. Prewarm the charset.

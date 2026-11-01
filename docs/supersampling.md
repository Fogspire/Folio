# Supersampling

A slanted or curved edge drawn smooth instead of stepped, by rasterising a document at more than one sample per pixel and averaging back down.

## Turn it on

```cpp
Widget->Supersample = 2;   // 1 to 4, each way
```

`Supersample` is also on the widget in the Details panel, under **Folio > Scale**. To try a document both ways without code, `folio.Show <document> 2` puts it on screen at two samples each way.

Then run `folio.Screens`. A supersampled document reports its sample count, the target it draws into and that target's size in MB, for example `supersample 2x into 3840x2160 (32 MB), 41 native glyph runs`.

Layout, line breaking, hit testing and text are the same at every sample count. Only how finely the shapes are rasterised changes. Text is drawn at the screen's own resolution after the rest, because glyphs are distance fields and are already exact.

## Reach for art first

Supersampling costs a target four times the size for the whole document to smooth a few edges in it. A slanted panel, a skewed button or a curved frame is better as art:

- **A vector sprite.** Draw the shape as an SVG and bake it with `msdfbake`. Its edge comes from a distance field, so it is exact at every angle, size and zoom, and costs what any sprite costs. See [vector art](vectors.md).
- **A baked PNG.** Build the shape as boxes, bake the element, and draw the PNG in its place with `decorator: image(<path> fill)`. In Folio Studio, Bake to PNG with 2 or 4 samples draws the element at that many times the pixels and averages them down, so the PNG is the element's own size with its edges already smooth. From the command line, `uipreview <document> --dp-ratio 2 --bake "#panel=UI/art/panel.png"` gives a PNG at twice the size, which smooths the same way when drawn at half. A bake with one sample keeps the steps it was drawn with.

Supersample what is left: a document with many slanted edges that move, or a design still changing too often to keep re-baking.

## Choosing a sample count

Use 2. Offer 1 and 2 in a game's settings, default to off, and treat 4 as a debugging curiosity.

The cost is quadratic in the sample count and in the resolution, and it is paid per **layer**, not per document. One RGBA8 target:

| | 1× | 2× | 4× |
|---|---|---|---|
| 1080p | 8 MB | 33 MB | **133 MB** |
| 1440p | 15 MB | 59 MB | **236 MB** |
| 4K | 33 MB | 133 MB | **531 MB** |

Multiply by the number of layers alive at once: the surface, one per element casting a shadow or carrying a filter, plus the scratch a blur or a mask needs. All are pooled between frames, so the peak is what is held. A screen with two shadows at 4K at 4× is over 2 GB of render targets.

Measured on one screen, counting columns crossing a shallow diagonal with no intermediate pixel (a hard step):

| | hard columns |
|---|---|
| no supersampling | 72 |
| 2× | 26 |
| 4× | 21 |

2× is worth four times the pixels. 4× takes 26 to 21, a difference nobody sees on a menu, for sixteen times the pixels and four times the memory of 2×.

On top of the targets, each frame pays one resolve pass and one composite. A document that has not changed since the last frame reuses its target and draws only its text, so a still screen pays once. One that animates or polls a data model every frame pays every frame.

It is a per-document choice: on for a screen of slanted panels, off for one that is mostly text.

## If it looks wrong

**Text looks softer with supersampling on.** Text inside a layer (a `filter`, a shadow, a mask, anything that composites as one image), under a clip mask, or under something drawn later goes through the target and is resampled. `folio.Screens` counts the glyph runs drawn at screen resolution: zero on a screen with words means something covers all of them, usually a full-screen sheen, scrim or vignette. Keep text out of layers and above full-screen overlays where the design allows.

**You cannot tell whether supersampling is softening text.** `folio.Supersample.NativeText 0` draws everything through the target, text included. Compare a screenshot each way: the text should be identical to the same document unsupersampled, with only the edges softer.

**The log says `a supersampled document wants a <W>x<H> target, which is <N> MB`.** The target is over 96 MB, and every layer the document pushes is another one that size. Use 2, or 1 at that resolution.

**The operating system warns about memory and the frame rate collapses.** The driver is spilling render targets into system memory. That is 4× at high resolution with a few layers; see the table above and use 2.

**You need to measure what it costs.** `stat Folio` shows the resolve as **RT: resolve supersample** and the pool as **Layer targets**; the GPU stat is **Folio supersample**. See the [profiling reference](profiling_reference.md).

# What Folio does not do yet

Everything here is a real limit, not a caveat. Each one is either scheduled, deliberately out of
scope, or known-broken and waiting on a fix. Read it before designing around something.

The complete list of what *does* work is **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**; this is the
other half.

---

## Renderer

### Layer effects are not compared between renderers

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` are drawn in OpenGL in `uipreview` and
replayed through RHI in the engine. Nothing compares those pixels.

**Layout under an effect is still exact** - `compare_renderers.sh` covers it, and an element under a
blur is in the same place in both. What is not promised is a shadow's colour, a blur's radius or the
exact falloff of a gradient at its edge.

A document using one gets a note in the VSCode preview saying so. Judge those in the engine.

### Layer effects need a layer, which a world-space document has none of

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` are all built on drawing into an offscreen
target and reading it back. A `UFolioWorldDocument` in Draw List mode records its geometry and
replays it into scene colour after the tonemapper, and there is nothing offscreen to read.

So on that path they are **skipped, with one warning naming the four properties**. The element still
draws, unfiltered and unmasked - the lesser wrong, since dropping it would hide the element itself.

`UI/examples/mask_image.rml` is the document to judge them by. Put it on a **render-target surface**,
where the whole layer stack is available and all four work. Seeing an unfiltered element in the world
with the mask painted over it means Draw List mode, not a broken effect.

### A layer costs about 3/255 of brightness

A surface drawn through a layer lands about `3/255` darker than the same colour drawn directly, from
the 8-bit sRGB round trip through the render target. Visible only side by side, and not worth a wider
target format.

### Colour will never match a screenshot of the game

The preview does no sRGB conversion where Slate linearises vertex colour and applies display gamma.
This is not a bug and will not be fixed - judge colour in the engine and treat the preview as
authoritative about everything else.

---

## Text

### No bidirectional text

Text is shaped with HarfBuzz, so ligatures, contextual forms, mark attachment and GPOS kerning all
work, and a right-to-left string is laid out right to left. What is missing is the Unicode
bidirectional algorithm: a paragraph that *mixes* directions, such as an Arabic sentence quoting a
Latin product name, puts the runs in the wrong order. Each run is individually correct.

Bidi is a layer above a shaper and would have to sit between RmlUi's line breaking and the font
engine. Nothing is stopping it; it has not been needed.

### Script runs are split by character, not by itemization

A string is cut into runs of one script each and shaped separately, with punctuation and digits
staying with whatever they follow. That is a simplification of what a full text engine does, and it
shows in one place: a font's shaping cannot reach across the boundary between two scripts.

### Small text has no hinting

Distance-field glyphs have no hinting, no stem snapping and no subpixel antialiasing. At 12-14dp on a
1080p display, stems are slightly less uniform than a hinted rasteriser would give.

Everywhere above that it is a wash or better, and under magnification it is not close. Check your
smallest body text on the target display. `--a11y` treats 12dp as a hard floor for the same reason.

### Line breaks move with the dp ratio

Advances are unhinted and scaled linearly from design units, which is what lets one atlas serve every
size. An advance is a whole number of pixels at each size, so doubling the ratio halves the relative
rounding error and words move.

**Never size a container to fit its text exactly at one ratio.** Check two with `--dp-ratio`.

### Font effects reach about 0.104em

An `outline`, `glow`, `shadow` or `blur` cannot extend past the field's range. Asked for more, it
clamps, with a warning naming the limit - and an effect asked for at three times the cap looks
identical to one asked for at the cap.

Raising it means raising `pxrange` when baking, at a quadratic cost in atlas area.

---

## Vector art

### `<use>` is dropped

The parser has no `<use>` handling at all, so an instanced shape contributes nothing and the drawing
bakes with pieces missing. The baker reports it by name, and `--strict` fails the run. Expand the
instances before exporting.

### Filled paths bake coarser than their own strokes

A filled region resolves slightly softer than a stroke of the same drawing at the same settings.
Noticeable only on art that has both, side by side.

### Conical and mesh gradients cannot bake

SVG has neither, so Affinity exports the fill as an embedded bitmap that the parser discards, and the
drawing bakes as an outline with nothing in it.

The baker reports this by name rather than leaving it to be found in the render, and `--strict` fails
the run. Export those fills as PNGs and use raster sprites.

### A gradient fill is a raster

Linear, radial and elliptical fills are evaluated per texel onto a colour sheet. The silhouette's
*edge* still comes from the field and stays exact; the fill softens under extreme magnification.

That is the trade rather than an oversight - a gradient is low frequency and upscales almost
invisibly, where an edge is high frequency and is the thing the field exists for.

---

## Accessibility

### Screen readers see one rectangle

Folio draws through a single Slate widget, so NVDA, JAWS and console TTS get one opaque rectangle and
nothing inside it. **No document structure, text or focus state reaches assistive technology.**

The static checks - contrast, text size, hit targets, control names, focus visibility - all work and
gate the build. The annotations they establish (`folio-label`, `folio-role`, `folio-hidden`) are
exactly what the screen-reader work will read, which is why annotating now costs nothing and
retrofitting later is what kills projects.

See **[accessibility](accessibility.md)**.

---

## Engine integration

### Surface mode is softer than the other draw modes

Not a bug and not fixable: Surface mode renders into an offscreen target that a material samples, so
the distance fields resolve at the *target's* resolution rather than the screen's and everything after
that is filtering. The other two modes evaluate them at final pixels, which is the whole point of
using fields.

It costs sharpness to buy layer effects and non-planar geometry. Use `After Tonemap` for anything
that needs neither, and raise `Max Surface Scale` past its default of 2 for a surface the player walks
up to.

See **[documents in the world](world_documents.md)**.

### The marker component has not been profiled

`UFolioMarkerDocument` works, but one document and one context per marker is a real cost and nobody
has measured where it stops being affordable. Thirty idle nameplates should be nearly free - an idle
document skips its update entirely - and thirty *animating* ones are thirty layouts a frame.

Measure with `stat Folio` before designing around a number. See
**[performance](performance_profiling.md)**.

---

## Authoring

### Baking is not automatic

`msdfbake` does not watch its source art. Re-run it after changing an SVG or a font, and remember
that RmlUi caches a texture by path forever - `folio.ReloadTextures` is what picks up a re-baked
atlas without reopening the document.

### There is no visual editor

Documents are hand-authored text. The VSCode preview will move an element by editing one property on
one rule, and that is the whole of it: it does not add rules, move elements between parents, or write
markup. That is deliberate.

---

## Where to go next

- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** — the complete list of what works.
- **[Testing a document](testing_a_document.md)** — the checks that catch most of what is catchable.

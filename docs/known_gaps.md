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

### `filter` other than `blur`, and `mask-image`

`brightness`, `contrast`, `grayscale`, `invert`, `opacity`, `saturate`, `sepia`, `hue-rotate` and
`mask-image` **parse, log once, and render nothing in the engine.** They all work in `uipreview`.

The layer machinery they need is in place - render targets, clip masks, the stencil path - and only
the shaders are missing. This is the closest of the renderer gaps to being closed.

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

### No RTL, no complex scripts, no ligatures

Shaping is kerning only, from FreeType's own kerning table. There is no HarfBuzz, so Arabic, Hebrew,
Thai and Devanagari will not shape correctly, and Latin ligatures do not form.

The glyph cache is keyed by **glyph index rather than codepoint**, which is the design decision that
makes HarfBuzz a drop-in later rather than a rewrite. Nothing else has to change.

Move this up the list if the game ships in any of those languages.

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

### A clip path escapes its clip

**Known broken.** A drawing whose geometry is clipped bakes with the clipped layer at its
*unclipped* extent, so the shape spills outside the drawing and the layer beneath it comes out solid.
Visible in `ui/examples/vector_gradient.rml` on the hatch sample.

Affects any Affinity export using a clip, which includes every hatch fill.

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

### Nothing covers the change-notification path

`FieldNotification` is how a view model tells a document that a value moved without polling. Nothing
in the plugin exercises it any more - the fixture that did was a dev document and went with them - so
a regression in that path would surface in a game rather than in the harness.

Games implementing `INotifyFieldValueChanged` should test it themselves. See
**[generating a view model](view_models.md)**.

### Public headers include private ones

`SFolioCanvas.h` includes `FolioNavigation.h` and `FolioInputState.h`, which are private. So **a game
module cannot include `SFolioCanvas.h`**, and no second module could compile against Folio.

Worth fixing on its own merits; it touches the header layout everything else builds on, which is why
it has not been.

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
- **[FOLIO_BRIEF.md](../FOLIO_BRIEF.md)** — the plan of record, including which of these are scheduled.

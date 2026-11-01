# What Folio does not do yet

Each limit here is scheduled, deliberately out of scope, or known-broken and waiting on a fix. Read it before designing around something.

What *does* work is in **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**.

---

## Renderer

### Layer effects are not compared between renderers

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` are drawn in OpenGL in `uipreview` and replayed through RHI in the engine. Nothing compares those pixels.

**Layout under an effect is still exact**: `compare_renderers.sh` covers it. What is not promised is a shadow's colour, a blur's radius or the exact falloff of a gradient at its edge.

The VSCode preview notes a document that uses one. Judge those in the engine.

### Layer effects need a layer, which only Surface mode has in the world

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` draw into an offscreen target and read it back. A `UFolioWorldDocument` in `After Tonemap` or `In Scene` mode records its geometry and replays it into the scene, and there is nothing offscreen to read.

So in those two modes they are **skipped, with one warning naming the four properties**. The element still draws, unfiltered and unmasked, rather than disappearing.

Judge them with `examples/mask_image.rml` on a document in **Surface** mode, where all four work. An unfiltered element in the world with the mask painted over it means one of the other two modes, not a broken effect.

### A layer costs about 3/255 of brightness

A surface drawn through a layer lands about `3/255` darker than the same colour drawn directly, from the 8-bit sRGB round trip through the render target. Visible only side by side; not worth a wider target format.

### A slanted edge steps

A background antialiases its edge across one pixel, as a browser does. On a shallow diagonal, such as a skewed panel, the steps stay visible however exact the blend. Draw such shapes as art, a vector sprite or a PNG baked with more than one sample; see [supersampling](supersampling.md#reach-for-art-first), the last resort.

### Colour will never match a screenshot of the game

The preview does no sRGB conversion where Slate linearises vertex colour and applies display gamma. This is by design: judge colour in the engine and trust the preview for everything else.

---

## Text

### No bidirectional text

Text is shaped with HarfBuzz, so ligatures, contextual forms, mark attachment and GPOS kerning all work, and a right-to-left string is laid out right to left. What is missing is the Unicode bidirectional algorithm: a paragraph that *mixes* directions, such as an Arabic sentence quoting a Latin product name, puts the runs in the wrong order. Each run is individually correct.

Bidi would sit between RmlUi's line breaking and the font engine, and gets added when a game needs it. If yours does, [open an issue](https://github.com/Fogspire/Folio/issues) saying so, and expect to be asked to test it in your game's languages before it ships.

### Script runs are split by character, not by itemization

A string is cut into runs of one script each and shaped separately, with punctuation and digits staying with whatever they follow. As a result a font's shaping cannot reach across the boundary between two scripts.

### Small text has no hinting

Distance-field glyphs have no hinting, no stem snapping and no subpixel antialiasing. At 12-14dp on a 1080p display, stem widths vary slightly more than a hinted rasteriser would give. Above that size it is a wash or better, and far better under magnification. Check your smallest body text on the target display. `--a11y` treats 12dp as a hard floor for the same reason.

### Line breaks move with the dp ratio

Advances are unhinted and scaled linearly from design units, so one atlas serves every size. An advance is a whole number of pixels at each size, so doubling the ratio halves the relative rounding error and words move.

**Never size a container to fit its text exactly at one ratio.** Check two with `--dp-ratio`.

### Font effects reach about 0.104em

An `outline`, `glow`, `shadow` or `blur` cannot extend past the field's range. Asked for more, it clamps, with a warning naming the limit.

Raising it means raising `pxrange` when baking, at a quadratic cost in atlas area.

### A revealing `<text>` builds its geometry every frame

The reveal paints each glyph with its own alpha, and RmlUi's own text geometry is generated once and cached, so a line that is typing regenerates its mesh per frame. It stops when the last character lands, and the runs go back to cached geometry.

Fine for a dialogue box, not for a screen of them. Nothing enforces a limit.

### `text-transform` and the reveal disagree

The reveal walks the laid-out characters against the ones it parsed, matching them so that collapsed whitespace and line breaks do not shift the schedule. A transform that changes their length breaks that match, and the reveal falls back to timing them in order: usually right, not guaranteed.

### A `<text>` holds prose and nothing else

No `<img>`, no data binding, no `{{ }}` (the braces belong to its markup). A line that has to name the player is a job for a term or for `SetMarkup` from game code.

---

## Vector art

### `<use>` is dropped

The parser has no `<use>` handling, so the drawing bakes with instanced pieces missing. The baker reports it by name, and `--strict` fails the run. Expand the instances before exporting.

### Filled paths bake coarser than their own strokes

A filled region resolves slightly softer than a stroke of the same drawing at the same settings. Noticeable only on art that has both, side by side.

### Conical and mesh gradients cannot bake

SVG has neither, so Affinity exports the fill as an embedded bitmap that the parser discards, and the drawing bakes as an outline with nothing in it.

The baker reports this by name, and `--strict` fails the run. Export those fills as PNGs and use raster sprites.

### A gradient fill is a raster

Linear, radial and elliptical fills are evaluated per texel onto a colour sheet. The silhouette's *edge* still comes from the field and stays exact; the fill softens under extreme magnification.

A gradient is low frequency and upscales almost invisibly; the edge is what the field is for.

---

## Accessibility

### A document in the world reaches only the game's own reader

`UFolioWorldDocument` and `UFolioMarkerDocument` draw into the scene rather than through a Slate widget, so there is no widget tree for NVDA or a console narrator to walk, and no screen rectangle to report as a control's bounds. **Screen documents are exposed to a platform reader; world and marker documents are not.**

The review cursor reads every live document whatever draws it, and `folio.Accessibility.Dump` prints them, marked as reachable that way only. So the game's own reader can read a terminal in the world and NVDA cannot.

Closing the gap is not scheduled: a terminal the player walks up to and uses should put its interface on a screen document while in use, which also helps anyone reading small text on a surface at an angle.

### Roles are limited to what the platform layer has

Unreal's accessibility layer maps a fixed list of widget types with no heading, region, landmark, table or progress bar. So to **a platform screen reader** a heading reads as text, a region as a pane and a progress bar as a read-only slider, and a document that leans on headings does not read as structured.

The game's own reader is not limited by that list: it says "heading", the review cursor jumps between them, and a table's cells carry their column names so those reach a platform reader anyway, as part of what each cell says. Fixing the platform half needs engine-side work on `EAccessibleWidgetType`, out of a plugin's reach.

### Only one speech back end ships

`SetScreenReader` talks through SAPI, which is Windows. Everywhere else it reports unavailable and turns itself off, so a console needs either its platform narrator (which reads the tree and needs none of this) or a back end written against that platform's speech API, installed with `Folio::FSpeech::SetBackend`. `ISpeechBackend` is six functions; the tests install one.

Folio cannot ship those: each platform's speech API is behind an SDK this repository cannot see.

### Text scale reaches `rem` and nothing else

The setting writes the root font size, so every rule written in `rem` moves and every rule written in `dp` or `px` stays where it is. The shipped theme sizes all of its type in `rem` against one `dp` root; a game that does not do the same gets a setting that half works.

`uipreview --text-scale 1.5 --a11y` names the text it could not move, per element.

### Reduced motion cannot reach motion that is drawn

It is a stylesheet rule plus a scroll behaviour: it stops every transition, every animation and the smooth scroll. A video, a render-target surface and anything a game writes from its own tick keep going.

`UFolioHudMotion` is wired to it and snaps. Anything else you drive per frame has to read `IsReducedMotionOn` itself.

A `<text>` typing itself out is **deliberately** outside it and has its own setting; see [accessibility](accessibility.md#text-speed).

### Reduced motion stops transition-completed events

Turning transitions off means a `transitionend` never arrives, because no transition ever starts. Nothing in the shipped library sequences that way; a game that does skips that step for the players most likely to have the setting on.

Sequence off a timer, an animation-completed callback or an explicit call instead.

### Announcements are not guaranteed to arrive

`folio-announce` and `FFolioAccessibilityTree::Announce` go through Unreal's `MakeAccessibleAnnouncement`, which is implemented on Windows, Mac and iOS and is documented as best-effort everywhere: two announcements close together can stomp one another. Never use one for something the player cannot get any other way.

The game's own reader queues them (one place per announcing element, dropped after four seconds unread), but a platform reader does not see that queue.

### The walk is the whole document, ten times a second

A document that changed is walked in full rather than patched, because RmlUi has no change notification covering everything a document can do to itself and a tree silently one edit behind is worse than no tree.

A walk is not per frame. Between walks the widgets follow their elements (one box read per node, no strings), so a HUD animating at 120fps pays that and walks ten times a second. `folio.Accessibility.WalkInterval` is the number, `stat Folio` reports the walk as *Accessibility walk*, and both are zero while nobody is listening.

One walk costs in proportion to the whole document. Measure a screen with two thousand elements under a reader.

### The flash check measures the whole frame

`--flash-check` averages luminance across the entire capture, where WCAG 2.3.1 defines the threshold over a ten-degree field. That is safe for false positives (a blinking caret cannot trip it) and risky for false negatives: a strobe over a quarter of the screen is diluted by the three quarters that are still.

It sees only what the preview renders. A flash coming from the scene behind a HUD, from a video, or from a shader whose time source the preview does not drive is outside it. Judge anything near the threshold by eye as well.

See **[accessibility](accessibility.md)**.

---

## Engine integration

### macOS and Linux are beta, and `uipreview` is Windows only

The plugin is developed and tested on Windows. It is enabled for macOS and Linux and the library build script supports both, but neither is tested by the author; report rough edges. `uipreview` is not ported; [Folio Studio](studio.md) runs on all three.

### Stereo has not been run on a headset

World documents draw correctly in both views under plain and instanced stereo, verified with `-emulatestereo`. Nobody has looked at one through a headset.

### Mock data does not hot reload in the engine

A change to `UI/mock/<model>.json` is not picked up by a running game; reopen the document. Studio and `uipreview` do reload it.

### Surface mode is softer than the other draw modes

By design: Surface mode renders into an offscreen target that a material samples, so the distance fields resolve at the *target's* resolution and everything after that is filtering. The other two modes evaluate them at final pixels.

Surface trades sharpness for layer effects and non-planar geometry. Use `After Tonemap` for anything that needs neither, and raise `Max Surface Scale` past its default of 2 for a surface the player walks up to.

See **[documents in the world](world_documents.md)**.

### The marker component has not been profiled

`UFolioMarkerDocument` works, but one document and one context per marker is a real cost, and nobody has measured where it stops being affordable. Thirty idle nameplates should be nearly free (an idle document skips its update), and thirty *animating* ones are thirty layouts a frame.

Measure with `stat Folio` before designing around a number. See **[performance](performance_profiling.md)**.

---

## Authoring

### The debugger takes the mouse from the document it inspects

`folio.Debugger` forces its host canvas interactive while it is **visible**, because its own documents live in that canvas's context and a hit-test-invisible canvas drops every click meant for them. So a HUD with the debugger open takes the mouse from the game: firing and looking stop until `folio.Debugger off`. The log says so when it takes it.

Hiding it is enough to give the mouse back; a game that changes `bInteractive` itself while the debugger is up loses that change when it hides.

### Baking is not automatic

`msdfbake` does not watch its source art. Re-run it after changing an SVG or a font. RmlUi caches a texture by path forever; `folio.ReloadTextures` picks up a re-baked atlas without reopening the document.

### The visual editor does not edit everything

[Folio Studio](studio.md) edits documents on a canvas as minimal changes to the text. What it cannot edit visually it still shows and inspects:

- **An element that comes from a template**, through `<body template="...">`, is not in the document where it appears on screen, so it cannot be selected for editing. Edit the template's own file.
- **Rows of a `data-for`** all edit the one element that was written. A single row cannot be edited; the markup has none.
- **`@keyframes`, `@spritesheet` and transitions** have no panel. Edit them as text in Code; they preview live.
- **Rules Folio ships** are never edited. A change to a value one of them sets is written as an override in your own rule.

### Studio's renderer differs where a blur reaches the edge of the screen

Studio draws with the same renderer as `uipreview`, compiled to WebAssembly, and `scripts/studio.sh parity` holds the two to within two colour levels on every recipe and example. The exception is a blur that samples past the edge of the viewport, such as a full-screen inset `box-shadow`: desktop OpenGL reads nothing there and WebGL, which cannot clamp a texture to a transparent border, reads the edge pixel. The difference is a few percent of pixels along the screen edge, at most 14 levels out of 255. Layout is unaffected.

### Studio's zoom draws at another resolution

Zoomed in, Studio draws the document with more pixels and a matching dp ratio, as the game does on a larger screen. Lengths in `dp` and `rem` scale and lengths in `px` do not, so a layout that leans on `px` moves while zoomed. Judge such a layout at 100%.

The drawn size is capped at sixteen million pixels, about 2.8x for a 1920x1080 document. Past that the canvas is stretched and softens.

### Studio is developed on Windows

It is built for macOS and Linux but not regularly run on either, the terminal panel included. Treat it as beta there.

---

## Where to go next

- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**: the complete list of what works.
- **[Testing a document](testing_a_document.md)**: the checks that catch most of what is catchable.

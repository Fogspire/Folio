# What Folio does not do yet

Everything here is a real limit, not a caveat. Each one is either scheduled, deliberately out of scope, or known-broken and waiting on a fix. Read it before designing around something.

The complete list of what *does* work is **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**; this is the other half.

---

## Renderer

### Layer effects are not compared between renderers

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` are drawn in OpenGL in `uipreview` and replayed through RHI in the engine. Nothing compares those pixels.

**Layout under an effect is still exact** - `compare_renderers.sh` covers it, and an element under a blur is in the same place in both. What is not promised is a shadow's colour, a blur's radius or the exact falloff of a gradient at its edge.

A document using one gets a note in the VSCode preview saying so. Judge those in the engine.

### Layer effects need a layer, which only Surface mode has in the world

`box-shadow`, `filter`, `backdrop-filter` and `mask-image` are all built on drawing into an offscreen target and reading it back. A `UFolioWorldDocument` in `After Tonemap` or `In Scene` mode records its geometry and replays it into the scene, and there is nothing offscreen to read.

So in those two modes they are **skipped, with one warning naming the four properties**. The element still draws, unfiltered and unmasked - the lesser wrong, since dropping it would hide the element itself.

`examples/mask_image.rml` is the document to judge them by. Put it on a document in **Surface** mode, where the whole layer stack is available and all four work. Seeing an unfiltered element in the world with the mask painted over it means one of the other two modes, not a broken effect.

### A layer costs about 3/255 of brightness

A surface drawn through a layer lands about `3/255` darker than the same colour drawn directly, from the 8-bit sRGB round trip through the render target. Visible only side by side, and not worth a wider target format.

### A slanted edge steps

A background antialiases its edge across one pixel, as a browser does. On a shallow diagonal, such as a skewed panel, the steps between pixels stay visible however exact the blend. Draw such shapes as art, a vector sprite or a PNG baked with more than one sample; see [supersampling](supersampling.md#reach-for-art-first), which is the last resort rather than the fix.

### Colour will never match a screenshot of the game

The preview does no sRGB conversion where Slate linearises vertex colour and applies display gamma. This is not a bug and will not be fixed - judge colour in the engine and treat the preview as authoritative about everything else.

---

## Text

### No bidirectional text

Text is shaped with HarfBuzz, so ligatures, contextual forms, mark attachment and GPOS kerning all work, and a right-to-left string is laid out right to left. What is missing is the Unicode bidirectional algorithm: a paragraph that *mixes* directions, such as an Arabic sentence quoting a Latin product name, puts the runs in the wrong order. Each run is individually correct.

Bidi is a layer above a shaper and would have to sit between RmlUi's line breaking and the font engine. Nothing is stopping it, and it will be added when a game needs it. If yours does, [open an issue](https://github.com/Fogspire/Folio/issues) saying so, and expect to be asked to test it in your game's languages before it ships.

### Script runs are split by character, not by itemization

A string is cut into runs of one script each and shaped separately, with punctuation and digits staying with whatever they follow. That is a simplification of what a full text engine does, and it shows in one place: a font's shaping cannot reach across the boundary between two scripts.

### Small text has no hinting

Distance-field glyphs have no hinting, no stem snapping and no subpixel antialiasing. At 12-14dp on a 1080p display, stems are slightly less uniform than a hinted rasteriser would give.

Everywhere above that it is a wash or better, and under magnification it is not close. Check your smallest body text on the target display. `--a11y` treats 12dp as a hard floor for the same reason.

### Line breaks move with the dp ratio

Advances are unhinted and scaled linearly from design units, which is what lets one atlas serve every size. An advance is a whole number of pixels at each size, so doubling the ratio halves the relative rounding error and words move.

**Never size a container to fit its text exactly at one ratio.** Check two with `--dp-ratio`.

### Font effects reach about 0.104em

An `outline`, `glow`, `shadow` or `blur` cannot extend past the field's range. Asked for more, it clamps, with a warning naming the limit - and an effect asked for at three times the cap looks identical to one asked for at the cap.

Raising it means raising `pxrange` when baking, at a quadratic cost in atlas area.

### A revealing `<text>` builds its geometry every frame

The reveal paints each glyph with its own alpha, and RmlUi's own text geometry is generated once and cached, so a line that is typing regenerates its mesh per frame. It stops the moment the last character lands: the runs are handed back to RmlUi and drawn from cached geometry like any other text.

That is fine for a dialogue box and would not be for a screen of them. Nothing enforces a limit.

### `text-transform` and the reveal disagree

The reveal walks the laid-out characters against the ones it parsed, matching them so that collapsed whitespace and line breaks do not shift the schedule. A transform that changes their length breaks that match, and the reveal falls back to timing them in order - usually still right, and not guaranteed to be.

### A `<text>` holds prose and nothing else

No `<img>`, no data binding, no `{{ }}` - the braces belong to its markup. A line that has to name the player is a job for a term or for `SetMarkup` from game code.

---

## Vector art

### `<use>` is dropped

The parser has no `<use>` handling at all, so an instanced shape contributes nothing and the drawing bakes with pieces missing. The baker reports it by name, and `--strict` fails the run. Expand the instances before exporting.

### Filled paths bake coarser than their own strokes

A filled region resolves slightly softer than a stroke of the same drawing at the same settings. Noticeable only on art that has both, side by side.

### Conical and mesh gradients cannot bake

SVG has neither, so Affinity exports the fill as an embedded bitmap that the parser discards, and the drawing bakes as an outline with nothing in it.

The baker reports this by name rather than leaving it to be found in the render, and `--strict` fails the run. Export those fills as PNGs and use raster sprites.

### A gradient fill is a raster

Linear, radial and elliptical fills are evaluated per texel onto a colour sheet. The silhouette's *edge* still comes from the field and stays exact; the fill softens under extreme magnification.

That is the trade rather than an oversight - a gradient is low frequency and upscales almost invisibly, where an edge is high frequency and is the thing the field exists for.

---

## Accessibility

### A document in the world reaches only the game's own reader

`UFolioWorldDocument` and `UFolioMarkerDocument` draw into the scene rather than through a Slate widget, so there is no widget tree for NVDA or a console narrator to walk - and no screen rectangle to report as a control's bounds if there were. **Screen documents are exposed to a platform reader; world and marker documents are not.**

They are not invisible, though: the review cursor reads every live document whatever draws it, and `folio.Accessibility.Dump` prints them, marked as reachable that way only. So a player using the game's own reader can read a terminal in the world and a player using NVDA cannot.

Closing the gap properly is not scheduled, because the answer for the case that matters is a different one: a terminal the player walks up to and uses should put its interface on a screen document while it is being used, which is also better for anyone reading small text on a surface at an angle.

### Roles are limited to what the platform layer has

Unreal's accessibility layer maps a fixed list of widget types, and there is no heading, region, landmark, table or progress bar in it. So to **a platform screen reader** a heading reads as text, a region as a pane and a progress bar as a read-only slider, and a document that leans on headings does not read as structured.

The game's own reader is not limited by that list: it says "heading", the review cursor jumps between them, and a table's cells carry their column names so those reach a platform reader anyway - as part of what each cell says rather than as a table. Fixing the platform half means engine-side work on `EAccessibleWidgetType`, which is not something a plugin can do.

### Only one speech back end ships

`SetScreenReader` talks through SAPI, which is Windows. Everywhere else it reports unavailable and turns itself off, so a console needs either its platform narrator - which reads the tree and needs none of this - or a back end written against that platform's own speech API, installed with `Folio::FSpeech::SetBackend`. `ISpeechBackend` is six functions and the seam is real rather than notional: the tests install one.

Folio cannot ship those, because each platform's speech API is behind an SDK this repository is not allowed to see.

### Text scale reaches `rem` and nothing else

The setting writes the root font size, so every rule written in `rem` moves and every rule written in `dp` or `px` stays where it is. The shipped theme sizes all of its type in `rem` against one `dp` root; a game that does not do the same gets a setting that half works.

`uipreview --text-scale 1.5 --a11y` names the text it could not move, per element, so this is visible rather than something a player discovers.

### Reduced motion cannot reach motion that is drawn

It is a stylesheet rule plus a scroll behaviour, so it stops every transition, every animation and the smooth scroll. A video, a render-target surface and anything a game writes from its own tick keep going.

`UFolioHudMotion` is wired to it and snaps. Anything else you drive per frame has to read `IsReducedMotionOn` itself; nothing can find it for you.

A `<text>` typing itself out is **deliberately** outside it and has its own setting - see [accessibility](accessibility.md#text-speed).

### Reduced motion stops transition-completed events

Turning transitions off means a `transitionend` never arrives, because no transition ever starts. Nothing in the shipped library sequences anything that way, and a game that does will find that step never happens for the players most likely to have the setting on.

Sequence off a timer, an animation-completed callback or an explicit call instead.

### Announcements are not guaranteed to arrive

`folio-announce` and `FFolioAccessibilityTree::Announce` go through Unreal's `MakeAccessibleAnnouncement`, which is implemented on Windows, Mac and iOS and is documented as best-effort everywhere: two announcements close together can stomp one another. Do not use one to carry something the player cannot get any other way.

The game's own reader queues them properly - one place per announcing element, dropped after four seconds if they have not been read - but that queue is Folio's and a platform reader does not see it.

### The walk is the whole document, ten times a second

A document that changed is walked in full rather than patched, because RmlUi has no change notification covering everything a document can do to itself and a tree silently one edit behind is worse than no tree.

What bounds it is that a walk is not per frame. Between walks the widgets follow their elements - one box read per node, no strings - so a HUD animating at 120fps pays that and walks ten times a second. `folio.Accessibility.WalkInterval` is the number, `stat Folio` reports the walk as *Accessibility walk*, and both are zero while nobody is listening.

The cost of one walk is still proportional to the whole document. A screen with two thousand elements under a reader is worth measuring.

### The flash check measures the whole frame

`--flash-check` averages luminance across the entire capture, where WCAG 2.3.1 defines the threshold over a ten-degree field. That is the safe direction for false positives - a blinking caret cannot trip it - and the risky one for false negatives: a strobe over a quarter of the screen is diluted by the three quarters that are still.

It also only sees what the preview renders. A flash coming from the scene behind a HUD, from a video, or from a shader whose time source the preview does not drive is outside it. Judge anything near the threshold by eye as well.

See **[accessibility](accessibility.md)**.

---

## Engine integration

### macOS and Linux are beta, and `uipreview` is Windows only

The plugin is developed and tested on Windows. It is enabled for macOS and Linux and the library build script supports both, but neither is tested by the author, so expect rough edges and report them. `uipreview` is not ported; [Folio Studio](studio.md) runs on all three.

### Stereo has not been run on a headset

World documents draw correctly in both views under plain and instanced stereo, verified with `-emulatestereo`. Nobody has looked at one through a headset.

### Mock data does not hot reload in the engine

A change to `UI/mock/<model>.json` is not picked up by a running game; reopen the document. Studio and `uipreview` do reload it.

### Surface mode is softer than the other draw modes

Not a bug and not fixable: Surface mode renders into an offscreen target that a material samples, so the distance fields resolve at the *target's* resolution rather than the screen's and everything after that is filtering. The other two modes evaluate them at final pixels, which is the whole point of using fields.

It costs sharpness to buy layer effects and non-planar geometry. Use `After Tonemap` for anything that needs neither, and raise `Max Surface Scale` past its default of 2 for a surface the player walks up to.

See **[documents in the world](world_documents.md)**.

### The marker component has not been profiled

`UFolioMarkerDocument` works, but one document and one context per marker is a real cost and nobody has measured where it stops being affordable. Thirty idle nameplates should be nearly free - an idle document skips its update entirely - and thirty *animating* ones are thirty layouts a frame.

Measure with `stat Folio` before designing around a number. See **[performance](performance_profiling.md)**.

---

## Authoring

### The debugger takes the mouse from the document it inspects

`folio.Debugger` forces its host canvas interactive while it is **visible**, because its own documents live in that canvas's context and a hit-test-invisible canvas drops every click meant for them. So a HUD with the debugger open is taking the mouse, and the game behind it is not: firing and looking stop until `folio.Debugger off`. It says so in the log when it takes it.

Tied to visibility rather than attachment so that hiding it is enough; a game that changes `bInteractive` itself while the debugger is up loses that change when it hides.

### Baking is not automatic

`msdfbake` does not watch its source art. Re-run it after changing an SVG or a font, and remember that RmlUi caches a texture by path forever - `folio.ReloadTextures` is what picks up a re-baked atlas without reopening the document.

### The visual editor does not edit everything

[Folio Studio](studio.md) edits documents on a canvas, and every edit it makes is a minimal change to the text. What it cannot edit visually it still shows and inspects:

- **An element that comes from a template**, through `<body template="...">`, is not in the document where it appears on screen, so it cannot be selected for editing. Edit the template's own file.
- **Rows of a `data-for`** all edit the one element that was written. There is no editing a single row, because there is no single row in the markup.
- **`@keyframes`, `@spritesheet` and transitions** have no panel. They are edited as text in Code, and preview live.
- **Rules Folio ships** are never edited. A change to a value one of them sets is written as an override in your own rule.

### Studio's renderer differs where a blur reaches the edge of the screen

Studio draws with the same renderer as `uipreview`, compiled to WebAssembly, and `scripts/studio.sh parity` holds the two to within two colour levels on every recipe and example. The exception is a blur that samples past the edge of the viewport, such as a full-screen inset `box-shadow`: desktop OpenGL reads nothing there and WebGL, which cannot clamp a texture to a transparent border, reads the edge pixel. The difference is a few percent of pixels along the screen edge, at most 14 levels out of 255. Layout is unaffected.

### Studio's zoom draws at another resolution

Zoomed in, Studio draws the document with more pixels and a dp ratio raised to match, the way the game draws it on a larger screen. Lengths in `dp` and `rem` scale and lengths in `px` do not, so a layout that leans on `px` moves while zoomed. Judge such a layout at 100%.

The drawn size is capped at sixteen million pixels, about 2.8x for a 1920x1080 document. Past that the canvas is stretched and softens.

### Studio is developed on Windows

It is built to run on macOS and Linux and is not regularly run on either, the terminal panel included. Treat it as beta there.

---

## Where to go next

- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**: the complete list of what works.
- **[Testing a document](testing_a_document.md)**: the checks that catch most of what is catchable.

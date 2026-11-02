# What Folio does not do yet

Every known limit, each with what to do instead. What *does* work is in **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**.

## Renderer

**Layer effects are not compared between renderers.** `box-shadow`, `filter`, `backdrop-filter` and `mask-image` draw in OpenGL in `uipreview` and through RHI in the engine, and nothing compares those pixels; layout under them is still covered by `compare_renderers.sh`. Judge a shadow's colour, a blur's radius or a gradient's edge falloff in the engine; the VSCode preview flags documents that use one.

**In the world, layer effects work only in Surface mode.** In `After Tonemap` and `In Scene` the four properties are skipped with one warning naming them, and the element draws unfiltered and unmasked. Use **Surface** mode for a world document that needs them, and judge them with `examples/mask_image.rml`.

**A layer costs about 3/255 of brightness.** The 8-bit sRGB round trip through the render target lands it slightly darker than the same colour drawn directly, visible only side by side. Nothing to do; it is not worth a wider target format.

**A slanted edge steps.** A background antialiases its edge across one pixel, so a shallow diagonal such as a skewed panel shows steps. Draw such shapes as art, a vector sprite or a PNG baked with more than one sample; [supersampling](supersampling.md#reach-for-art-first) is the last resort.

**Colour in the preview never matches a screenshot of the game.** The preview does no sRGB conversion, where Slate linearises vertex colour and applies display gamma. Judge colour in the engine and trust the preview for everything else.

## Text

**No bidirectional text.** HarfBuzz shaping works, and a right-to-left string lays out right to left, but a paragraph that mixes directions puts its runs in the wrong order. Keep each paragraph to one direction; if your game needs bidi, [open an issue](https://github.com/Fogspire/Folio/issues) and expect to test it in your game's languages before it ships.

**Shaping cannot reach across scripts.** A string is cut into runs of one script each (punctuation and digits stay with whatever they follow) and each run is shaped on its own. Nothing to do unless a font relies on cross-script shaping.

**Small text has no hinting.** Distance-field glyphs have no hinting, stem snapping or subpixel antialiasing, so at 12-14dp on a 1080p display stem widths vary slightly. Check your smallest body text on the target display; `--a11y` treats 12dp as a hard floor.

**Line breaks move with the dp ratio.** Advances are unhinted and rounded per size, so words move between ratios. Never size a container to fit its text exactly at one ratio; check two with `--dp-ratio`.

**Font effects reach about 0.104em.** An `outline`, `glow`, `shadow` or `blur` past that clamps, with a warning naming the limit. Re-bake with a higher `pxrange` for more, at a quadratic cost in atlas area.

**A revealing `<text>` rebuilds its geometry every frame while it types.** It returns to cached geometry when the last character lands, and nothing enforces a limit. Fine for a dialogue box; avoid a screen full of them typing at once.

**`text-transform` and the reveal disagree.** A transform that changes the text's length breaks the reveal's character matching, and it falls back to timing characters in order: usually right, not guaranteed. Avoid length-changing transforms on revealing text.

**A `<text>` holds prose and nothing else.** No `<img>`, no data binding, no `{{ }}` (the braces belong to its markup). To name the player, use a term or `SetMarkup` from game code.

## Vector art

**`<use>` is dropped.** The drawing bakes with instanced pieces missing; the baker reports it by name and `--strict` fails the run. Expand the instances before exporting.

**Filled paths bake coarser than their own strokes.** A fill resolves slightly softer than a stroke of the same drawing, noticeable only side by side. Nothing to do.

**Conical and mesh gradients cannot bake.** Affinity exports them as an embedded bitmap the parser discards, so the drawing bakes as an empty outline; the baker reports it and `--strict` fails the run. Export those fills as PNGs and use raster sprites.

**A gradient fill is a raster.** Linear, radial and elliptical fills are evaluated per texel, so the fill softens under extreme magnification while the silhouette's edge stays exact. Nothing to do; gradients upscale almost invisibly.

## Paths

**A path is one flat colour per stroke and fill.** No gradients. For shaded art, use a vector sprite or a PNG.

**A crossing translucent stroke darkens where it overlaps itself.** Each pass over the same pixels blends again, unlike SVG, which composites the stroke once. Opaque strokes are unaffected, and a fill is drawn once wherever it overlaps itself.

**A hairpin turn shows faint light specks inside the stroke.** Where the two sides of a sharp turn overlap, their antialiased edges brighten each other slightly. Nothing to do; it is under a tenth of the stroke's brightness.

## Accessibility

**World and marker documents reach only the game's own reader.** `UFolioWorldDocument` and `UFolioMarkerDocument` have no widget tree for NVDA or a console narrator; screen documents do. The review cursor still reads them and `folio.Accessibility.Dump` lists them; for a terminal the player uses, put its interface on a screen document while in use.

**Roles are limited to what the platform layer has.** To a platform screen reader a heading reads as text, a region as a pane and a progress bar as a read-only slider. The game's own reader announces headings and table columns properly; the platform half needs engine work on `EAccessibleWidgetType`.

**Only one speech back end ships.** `SetScreenReader` uses SAPI, so outside Windows it reports unavailable and turns itself off. On a console use the platform narrator, or write an `ISpeechBackend` (six functions) against the platform's speech API and install it with `Folio::FSpeech::SetBackend`.

**Text scale reaches `rem` and nothing else.** Rules in `dp` or `px` stay where they are. Size your type in `rem` against one `dp` root as the shipped theme does, and run `uipreview --text-scale 1.5 --a11y` to list text that could not move.

**Reduced motion cannot reach motion that is drawn.** It stops transitions, animations and smooth scroll, but not a video, a render-target surface or anything a game writes from its own tick. Read `IsReducedMotionOn` in anything you drive per frame (`UFolioHudMotion` already does); a typing `<text>` has its own [text speed](presentation_settings.md#text-speed) setting.

**Reduced motion stops transition-completed events.** No transition starts, so no `transitionend` arrives. Sequence off a timer, an animation-completed callback or an explicit call instead.

**Announcements are not guaranteed to arrive.** `folio-announce` and `FFolioAccessibilityTree::Announce` go through Unreal's best-effort `MakeAccessibleAnnouncement` (Windows, Mac and iOS), where two close together can stomp one another. Never use one for something the player cannot get any other way.

**The accessibility walk covers the whole document, ten times a second.** A changed document is walked in full, at a cost proportional to its size, and nothing is walked while nobody is listening. Measure a large screen under a reader with `stat Folio` (*Accessibility walk*); `folio.Accessibility.WalkInterval` sets the rate.

**The flash check measures the whole frame.** `--flash-check` averages luminance over the entire capture, so a strobe over part of the screen is diluted, and it sees nothing the preview does not render (the scene, video, shaders on their own clock). Judge anything near the threshold by eye as well; see **[accessibility](accessibility.md)**.

## Engine integration

**macOS and Linux are beta, and `uipreview` is Windows only.** The plugin is enabled and builds on both but is not tested there by the author. Report rough edges; use [Folio Studio](studio.md), which runs on all three.

**Stereo has not been run on a headset.** World documents draw correctly in both views under plain and instanced stereo with `-emulatestereo`. Check on your headset before relying on it.

**Mock data does not hot reload in the engine.** A change to `UI/mock/<model>.json` is not picked up by a running game. Reopen the document; Studio and `uipreview` do reload it.

**Surface mode is softer than the other draw modes.** It resolves the document at its render target's resolution rather than at final pixels. Use `After Tonemap` unless you need layer effects or non-planar geometry, and raise `Max Surface Scale` past 2 for a surface the player walks up to; see **[documents in the world](world_documents.md#if-it-looks-wrong)**.

**The marker component has not been profiled.** Each `UFolioMarkerDocument` is one document and one context; idle ones skip their update, animating ones each lay out every frame. Measure with `stat Folio` before designing around a number; see **[performance](performance_profiling.md)**.

## Authoring

**The debugger takes the mouse while it is visible.** `folio.Debugger` forces its host canvas interactive, so firing and looking stop, and the log says so. Hide it or run `folio.Debugger off`.

**Baking is not automatic.** `msdfbake` does not watch its source art. Re-run it after changing an SVG or a font, then `folio.ReloadTextures` to pick up the new atlas without reopening the document.

**[Folio Studio](studio.md) does not edit everything visually.** It still shows and inspects these; edit them as follows:

- **An element from a template** (`<body template="...">`): edit the template's own file.
- **A row of a `data-for`**: every row edits the one element written in the markup.
- **`@keyframes`, `@spritesheet` and transitions**: edit them as text in Code; they preview live.
- **Rules Folio ships**: never edited; a change is written as an override in your own rule.

**Studio's renderer differs where a blur reaches the edge of the screen.** A blur sampling past the viewport, such as a full-screen inset `box-shadow`, differs by up to 14 levels out of 255 on a few edge pixels; everything else matches `uipreview` within two levels and layout is unaffected. Judge such blurs in the engine.

**Studio's zoom draws at another resolution.** Lengths in `dp` and `rem` scale with zoom and `px` does not, and past sixteen million pixels (about 2.8x for 1920x1080) the canvas stretches and softens. Judge a `px`-based layout at 100%.

**Studio is developed on Windows.** It builds for macOS and Linux but is not regularly run there, terminal panel included. Treat it as beta on those platforms.

## Where to go next

- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**: the complete list of what works.
- **[Testing a document](testing_a_document.md)**: the checks that catch most of what is catchable.

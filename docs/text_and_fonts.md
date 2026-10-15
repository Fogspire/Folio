# Text and fonts

Folio replaces RmlUi's font engine outright. Glyphs are **MTSDF**: three channels of multi-channel distance field whose median reconstructs sharp corners, plus a true signed distance in alpha that the effects read.

The practical consequence is that **type scales freely**. One size-independent atlas per face serves 13dp and 96dp identically, and the same text stays sharp under `transform: scale`, at 4K, and at any DPI. Pick sizes for the design, not for what an atlas was baked at.

## Getting a face

`font-family` accepts **one** family only. There is no fallback stack and there is no `@font-face` - faces are registered by the host.

Folio bundles Inter and Roboto Mono, so `font-family: Inter` works in a new project with no setup. A game adds its own by dropping files in `UI/fonts/`; the preview takes `--font <path>`.

```css
body { font-family: Inter; font-size: 16dp; }
.code { font-family: Roboto Mono; }
```

A family name is one Folio registered, not a filename. Resolution order is `--font` → the game's `UI/fonts/` → the plugin's bundled faces → the engine's Roboto.

**The bundled two are a floor, not the palette.** A screen with a voice of its own wants a face that has one, and [fonts.google.com](https://fonts.google.com) is an open-licensed library that covers most of them - fetch the `.ttf`, put it in `UI/fonts/`, ship the licence beside it. Designing around Inter because it is what is already there is the wrong reason to pick a typeface.

**Nothing renders at all until a family is named.** RCSS's initial `font-family` is empty, so a document with no family set is blank rather than wrong-looking.

## Weight

```css
.light  { font-weight: 300; }
.body   { font-weight: normal; }   /* 400 */
.strong { font-weight: 600; }
.heavy  { font-weight: bold; }     /* 700 */
```

On a variable face any value in 1-1000 is real - it drives the `wght` axis directly rather than snapping to the nearest cut. Values are quantised in steps of 50, so `375` and `425` are distinct instances and `375` and `380` are not.

There are no relative weights: `bolder` and `lighter` do not exist.

`opsz`, where a face has it, is driven automatically from the requested size. Other axes - `wdth` and anything custom - are pinned at registration and reached by family name:

```cpp
// On the font engine, which the module owns. Style and the pinned axes are both explicit, because a
// family is what RCSS can name and everything else has to be baked into one.
FFolioModule::Get().GetFontEngine()->RegisterVariableFace(
    "<path>", "Inter Condensed", Rml::Style::FontStyle::Normal, {{"wdth", 75.0f}});
```

```css
.narrow { font-family: Inter Condensed; }
```

## Size, and the one real trade-off

Distance-field text has **no hinting and no stem snapping**. At 12-14dp on a 1080p display, stems are slightly less uniform than a hinted rasteriser would give. Everywhere above that it is a wash or better, and under any magnification it is not close.

So: **look at your smallest body text on the target display before committing to it.** That is the one place this technology costs you something, and it is worth ten minutes rather than a discovery later. `--a11y` treats 12dp as a hard floor for the same reason - see **[accessibility](accessibility.md)**.

Two related facts that bite in layout rather than in looks:

**Advances are unhinted and scaled linearly from design units.** That is what lets one atlas serve every size, and it means text wraps in slightly different places than a hinted rasteriser would.

**Line breaks do not survive a change of dp ratio.** An advance is a whole number of pixels at each size, so doubling the ratio halves the relative rounding error and words move. Never size a container to fit its content exactly at one ratio - check two with `--dp-ratio`.

## Font effects

Four, and they are the only way to get a text shadow - `text-shadow` does not exist.

```css
.title  { font-effect: outline(2dp #000000); }
.glow   { font-effect: glow(3dp #f0c674); }
.drop   { font-effect: shadow(2dp 2dp #000000a0); }
.soft   { font-effect: blur(4dp #ffffff40); }
```

All four are resolved from the field's true-distance channel rather than baked into extra glyph textures, so each costs one more quad per glyph and no atlas at all.

Widths and offsets take `dp`, `px` or `em`. Write them in `dp` like everything else and they keep their proportion to the text at every UI scale; `px` stays the same number of pixels whatever the scale.

**Their reach is bounded, and the bound is in em, not pixels** - currently 0.104em at the default atlas settings. `outline(2px)` is a twentieth of an em on 40dp text and well inside it; on 13dp text it is a sixth of an em and gets clamped, with a warning naming the limit.

Clamping is a hard limit, not a soft one. An effect asked for at three times the cap looks identical to one asked for at the cap. **Size an effect against the text it decorates**, and check small text specifically. To go wider, raise `pxrange` when baking, at a quadratic cost in atlas area.

## Textured text

`fill-image` fills the glyphs with a pattern instead of a flat colour - a scanline, a gradient, gold leaf:

```css
.crt { font-effect: fill-image("/UI/art/scanlines.png" 4dp); }
```

A path, a tile size, and an optional colour the pattern is multiplied by. Three things to know:

- **It names a file, not a sprite.** A font effect is built while the stylesheet is parsed and has no route to a spritesheet.
- **The pattern is anchored to the screen, not to the text.** A scanline stays put while text scrolls under it - which is what a scanline is for, and wrong for anything meant to travel with the words.
- **It only textures the fill.** It is a front layer over the glyph, so an `outline`, `glow` or `shadow` on the same text stays flat. Where the pattern is transparent the flat `color` shows through, so the two compose.

Not available inside a layer - text under a `filter`, a `mask-image`, or an `opacity` on an ancestor. Those paths bind one texture and the pattern is skipped.

## Shaping

Text is shaped with HarfBuzz, so a string becomes glyphs the way the font says it should rather than one glyph per character. That gets you ligatures, contextual forms, correctly attached combining marks, and the kerning a modern face actually ships, which lives in GPOS and is invisible to anything reading the old `kern` table.

Nothing in markup asks for it and there is no property to turn it on. The one control is `font-kerning: none`, which switches off the `kern` feature and leaves everything else.

Shaping is per script: a string is cut into runs of one script each and each run shaped on its own, with punctuation and digits staying with whatever they follow. Right-to-left text lays out right to left. Mixing the two directions in one paragraph does not work, and is the one gap - see [known gaps](known_gaps.md).

`folio.Font.Shaping 0` (or `uipreview --simple-shaping`) drops back to one glyph per codepoint with legacy kerning, which is what a build without HarfBuzz does. It is there so a metrics diff has something to diff against; text reflows when it changes.

## Text properties

`font` `font-family` `font-size` `font-style` `font-weight` `font-kerning` · `font-effect` · `text-align` `text-decoration` `text-transform` `text-overflow` · `letter-spacing` `line-height` · `white-space` `word-break`.

Worth knowing:

- **`text-align: justify` parses and does nothing.** Justification is not implemented; the line falls back to left.
- **`line-height` has no `normal`.** Always give a number, length or percentage.
- **`font-style` has no `oblique`.**
- **`text-overflow` takes `clip`, `ellipsis`, or a string** to use in place of the ellipsis.
- `white-space`: `normal`, `pre`, `nowrap`, `pre-wrap`, `pre-line`. `word-break`: `normal`, `break-all`, `break-word`.

## Baking and caching

Glyphs are generated on demand and cached to disk, so the first run of a face costs a moment and every run after it does not. `Tools/msdfbake font` pre-bakes a face for a packaged build.

`folio.Font.Report` (or `uipreview --font-report`) prints which faces are loaded, their variation tuples, and what they cost in pages and glyphs. It is the quickest way to find out that a screen quietly instantiated nine weights.

## Where to go next

- **[Rich text and the typewriter](rich_text.md)**: emphasis inside a translatable line, names shared across the game, and text that types itself out.
- **[Vector art](vectors.md)**: the same distance fields, for icons.
- **[Localisation](localization.md)**: translated strings run about 40% longer; check with `--culture pseudo` before it matters.
- **[Accessibility](accessibility.md)**: the size floor and the contrast check.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §6: the full reference.

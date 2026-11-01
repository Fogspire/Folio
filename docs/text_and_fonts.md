# Text and fonts

Folio replaces RmlUi's font engine outright. Glyphs are **MTSDF**: three channels of multi-channel distance field whose median reconstructs sharp corners, plus a true signed distance in alpha that the effects read.

So **type scales freely**. One size-independent atlas per face serves 13dp and 96dp identically, and text stays sharp under `transform: scale`, at 4K, and at any DPI.

## Getting a face

`font-family` accepts **one** family only, with no fallback stack. The host registers faces, or a stylesheet declares them with `@font-face`.

Folio bundles Inter and Roboto Mono, so `font-family: Inter` works in a new project with no setup. A game adds its own by dropping files in `UI/fonts/`; the preview takes `--font <path>`. **Once `UI/fonts/` has a face in it, the bundled two are not loaded**, so a document naming `Inter` there needs Inter in `UI/fonts/` too.

```css
body { font-family: Inter; font-size: 16dp; }
.code { font-family: Roboto Mono; }
```

A family name is one Folio registered, not a filename. The faces come from the first of these that has any: `--font`, the game's `UI/fonts/`, the plugin's bundled faces, the engine's Roboto. They are alternatives, not a stack.

**The bundled two are a floor, not the palette.** [fonts.google.com](https://fonts.google.com) is an open-licensed library: fetch the `.ttf`, put it in `UI/fonts/`, ship the licence beside it.

### `@font-face`

```css
@font-face { font-family: "Hand"; src: url(fonts/hand.ttf); }
@font-face { font-family: "Hand"; src: url(fonts/hand-bold.ttf); font-weight: bold; }
@font-face { font-family: "Display"; src: url(fonts/display-variable.ttf); font-weight: 100 900; }
```

`src` resolves against the stylesheet. `font-weight` takes `normal`, `bold`, 1 to 1000, or a range for a variable file. `font-style` takes `normal` or `italic`.

**Faces are global, not per document.** The first declaration of a family, style and weight is the one every document gets, for as long as the game runs. A later one naming a different file is ignored with a warning, which `--strict` fails on. A face of the same family, style and weight that the host registered first still wins.

**Nothing renders until a family is named.** RCSS's initial `font-family` is empty, so a document with no family set is blank.

## Weight

```css
.light  { font-weight: 300; }
.body   { font-weight: normal; }   /* 400 */
.strong { font-weight: 600; }
.heavy  { font-weight: bold; }     /* 700 */
```

On a variable face any value in 1-1000 drives the `wght` axis directly rather than snapping to the nearest cut. Values are quantised in steps of 50, so `375` and `425` are distinct instances and `375` and `380` are not.

`bolder` and `lighter` do not exist.

`opsz`, where a face has it, is driven automatically from the requested size. Other axes (`wdth` and anything custom) are pinned at registration and reached by family name:

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

Distance-field text has **no hinting and no stem snapping**. At 12-14dp on a 1080p display, stem widths vary slightly more than a hinted rasteriser would give. Above that it is a wash or better.

**Look at your smallest body text on the target display before committing to it.** `--a11y` treats 12dp as a hard floor for the same reason; see **[accessibility](accessibility.md)**.

Two related facts bite in layout:

**Advances are unhinted and scaled linearly from design units**, so text wraps in slightly different places than a hinted rasteriser would.

**Line breaks do not survive a change of dp ratio.** An advance is a whole number of pixels at each size, so doubling the ratio halves the relative rounding error and words move. Never size a container to fit its content exactly at one ratio; check two with `--dp-ratio`.

## Font effects

Four, and the only way to get a text shadow: `text-shadow` does not exist.

```css
.title  { font-effect: outline(2dp #000000); }
.glow   { font-effect: glow(3dp #f0c674); }
.drop   { font-effect: shadow(2dp 2dp #000000a0); }
.soft   { font-effect: blur(4dp #ffffff40); }
```

All four read the field's true-distance channel, so each costs one more quad per glyph and no atlas.

Widths and offsets take `dp`, `px` or `em`. `dp` keeps its proportion to the text at every UI scale; `px` stays the same number of pixels.

**Their reach is bounded in em, not pixels**: 0.104em at the default atlas settings. `outline(2px)` is a twentieth of an em on 40dp text and well inside it; on 13dp text it is a sixth of an em and gets clamped, with a warning naming the limit.

An effect asked for at three times the cap looks identical to one at the cap. **Size an effect against the text it decorates**, and check small text specifically. To go wider, raise `pxrange` when baking, at a quadratic cost in atlas area.

## Textured text

`fill-image` fills the glyphs with a pattern instead of a flat colour:

```css
.crt { font-effect: fill-image("/UI/art/scanlines.png" 4dp); }
```

A path, a tile size, and an optional colour the pattern is multiplied by.

- **It names a file, not a sprite.** A font effect is built while the stylesheet is parsed and has no route to a spritesheet.
- **The pattern is anchored to the screen, not to the text.** A scanline stays put while text scrolls under it, which is wrong for anything meant to travel with the words.
- **It only textures the fill.** It is a front layer over the glyph, so an `outline`, `glow` or `shadow` on the same text stays flat. Where the pattern is transparent the flat `color` shows through, so the two compose.

Not available inside a layer (text under a `filter`, a `mask-image`, or an `opacity` on an ancestor): those paths bind one texture and the pattern is skipped.

## Shaping

Text is shaped with HarfBuzz: ligatures, contextual forms, attached combining marks, and the GPOS kerning a modern face ships, which a reader of the old `kern` table misses.

Shaping is always on. The one control is `font-kerning: none`, which switches off the `kern` feature and leaves everything else.

Shaping is per script: a string is cut into runs of one script each and each run shaped on its own, with punctuation and digits staying with whatever they follow. Right-to-left text lays out right to left. Mixing the two directions in one paragraph does not work; see [known gaps](known_gaps.md).

`folio.Font.Shaping 0` (or `uipreview --simple-shaping`) drops back to one glyph per codepoint with legacy kerning, which is what a build without HarfBuzz does. It exists for metrics diffs; text reflows when it changes.

## Text properties

`font` `font-family` `font-size` `font-style` `font-weight` `font-kerning` · `font-effect` · `text-align` `text-decoration` `text-transform` `text-overflow` · `letter-spacing` `line-height` · `white-space` `word-break`.

- **`text-align: justify` parses and does nothing.** Justification is not implemented; the line falls back to left.
- **`line-height` has no `normal`.** Always give a number, length or percentage.
- **`font-style` has no `oblique`.**
- **`text-overflow` takes `clip`, `ellipsis`, or a string** to use in place of the ellipsis.
- `white-space`: `normal`, `pre`, `nowrap`, `pre-wrap`, `pre-line`. `word-break`: `normal`, `break-all`, `break-word`.

## Baking and caching

Glyphs are generated on demand and cached to disk, so only the first run of a face pays for them. `Tools/msdfbake font` pre-bakes a face for a packaged build.

`folio.Font.Report` (or `uipreview --font-report`) prints which faces are loaded, their variation tuples, and what they cost in pages and glyphs. Use it to catch a screen that instantiated nine weights.

## Where to go next

- **[Rich text and the typewriter](rich_text.md)**: emphasis inside a translatable line, names shared across the game, and text that types itself out.
- **[Vector art](vectors.md)**: the same distance fields, for icons.
- **[Localisation](localization.md)**: translated strings run about 40% longer; check with `--culture pseudo` before it matters.
- **[Accessibility](accessibility.md)**: the size floor and the contrast check.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §6: the full reference.

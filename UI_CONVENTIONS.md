# Folio: what RCSS actually supports

**This document is authoritative for capability.** Anything authoring `.rml` or `.rcss` against Folio, human or model, follows it. Where it conflicts with general CSS/HTML knowledge, this document wins.

RCSS is based on CSS2 with selected CSS3 features. It is **not** a CSS3 subset and it is **not** a browser. The most common failure mode when writing for it is emitting valid-looking CSS that RmlUi silently ignores.

This file describes the engine. A game layered on Folio will have its own conventions document covering house style: token palettes, directory structure, which components to compose, when a string must be localised. That one governs taste; this one governs what exists.

> **Verify on version bump.** Everything below was checked against the **RmlUi 6.2** source tree
> that Folio pins, not against the documentation site. The docs site is built from RmlUi's
> `master` and documents unreleased features as if they shipped, so it is not a safe reference
> for what works today. When the pin moves, re-derive this file from
> `Source/Core/StyleSheetSpecification.cpp` (properties), `Source/Core/Factory.cpp` (elements,
> decorators, filters, font effects, data views) and `Source/Core/StyleSheetFactory.cpp`
> (structural selectors).

---

## 1. Supported properties

This is the complete set. **A property not on this list does not exist.**

**Layout & box model** `display` (inline, block, inline-block, flow-root, flex, inline-flex, table, inline-table, table-row-group, table-row, table-column-group, table-column, table-cell, none) · `position` (static, relative, absolute, fixed) · `top` `right` `bottom` `left` `inset` · `float` `clear` · `width` `height` `min-width` `max-width` `min-height` `max-height` · `margin` (+ per-edge) · `padding` (+ per-edge) · `box-sizing` · `overflow` `overflow-x` `overflow-y` · `overscroll-behavior` · `clip` · `z-index` · `vertical-align`

**Flexbox**: fully supported, and the primary layout tool `flex` `flex-basis` `flex-direction` `flex-flow` `flex-grow` `flex-shrink` `flex-wrap` · `align-content` `align-items` `align-self` · `justify-content` · `gap` `row-gap` `column-gap`

**Colour & surface** `color` · `background-color` · `background` (colour only) · `opacity` · `border-width` (+ per-edge) · `border-color` (+ per-edge) · `border` and `border-top`/`-right`/`-bottom`/`-left` (width + colour only) · `border-radius` (+ per-corner, percentages, `/` for elliptical corners) · `box-shadow` · `image-color` · `decorator` · `mask-image` · `filter` · `backdrop-filter`

**Text & fonts** `font` `font-family` `font-size` `font-style` `font-weight` `font-kerning` · `font-effect` · `text-align` `text-decoration` `text-transform` `text-overflow` · `letter-spacing` `line-height` · `white-space` `word-break`

**Animation** `transition` · `animation` · `transform` `transform-origin` · `perspective` `perspective-origin`

**Interaction** `nav` `nav-up` `nav-right` `nav-down` `nav-left` · `tab-index` · `focus` · `pointer-events` · `cursor` · `caret-color` · `drag` · `visibility`

**Text reveal**: `<text>` only, and all inherited `reveal` (none, typewriter) · `reveal-rate` · `reveal-fade` · `reveal-rise` · `reveal-punctuation` · `reveal-sound` · `reveal-start` (auto, manual)

**Misc** `fill-image` (progress elements) · `scrollbar-margin`

**At-rules:** `@spritesheet`, `@keyframes`, `@media`, `@decorator`. **That is the complete set**: there is no `@import` and no `@font-face`. See §10 for how stylesheets are composed without `@import`.

### Defaults that differ from a browser

RmlUi has no user-agent stylesheet, so these are the raw initial values every element starts from. Folio's `/folio/folio.rcss` fixes the first one; the rest are live.

| Property | RCSS initial value | Consequence |
|---|---|---|
| `display` | `inline` | Every element, including `div` and `p`. Without `folio.rcss` a document is one run of overlapping text |
| `color` | `white` | Not black |
| `font-size` | `12px` | Small, and in `px`: it does not scale with dp-ratio until you set it |
| `font-family` | *(empty)* | No text renders at all until a family is named |
| `line-height` | `1.2` | A unitless multiplier, and there is no `normal` |
| `background-color` | `transparent` | |
| `box-sizing` | `content-box` | Padding and border add to `width`. Most components want `border-box` |
| `tab-index` | `none` | Nothing is focusable until you say so |
| `nav-*` | `none` | Directional input does nothing at all until you say so. See §7 |
| `text-align` | `left` (inherited) | |
| `focus` | `auto` (inherited) | |

`width` on the document `body` also needs setting explicitly: a document is laid out shrink-to-fit against the context regardless of `display`. `folio.rcss` sets `width: 100%`; add `height: 100%` yourself if you want a full-viewport background.

---

## 2. Banned: will silently do nothing

| Instead of | Use |
|---|---|
| `display: grid` and all `grid-*` properties | flexbox |
| `var()` and `--custom-properties` | concrete classes in a token stylesheet |
| `background-image`, `background-size`, `background-position`, `background-repeat` | `decorator: image(...)` |
| `border-style` | `border` gives width and colour and is always solid; `decorator: dashed-border(...)` or `dotted-border(...)` for the other two |
| `text-shadow` | `font-effect: shadow(...)` |
| `outline` | `border` or a decorator |
| `object-fit` | the image decorator's fit modes (`fill`, `contain`, `cover`, `scale-none`, `scale-down`, `repeat`) |
| `position: sticky` | `fixed` or `absolute` |
| `::before` / `::after` / `content` | a real element |
| `aspect-ratio`, `writing-mode`, `list-style-*`, `font-variant`, `font-stretch` | no equivalent: restructure |
| `@import` | one `<link>` per stylesheet, or a single template link. See §10 |
| `@font-face` | faces are registered by the host: `uipreview --font`, plugin settings in engine |
| `calc()`, `clamp()`, `min()`, `max()` | precomputed values, or flexbox for proportional sizing |
| `:has()` | restructure, or set a class from C++ |
| `:disabled` on a generic element | a class such as `.is-disabled`. On a real form control it does work; see below |
| `start` / `end` / `normal` in flex properties | `flex-start` / `flex-end` / `stretch` |

**Value-level restrictions that are easy to miss:**

- `text-align: justify` parses without complaint and then does nothing: justification is not implemented, and the line falls back to left-aligned
- `line-height` has no `normal`: always give a number, length or percentage
- `font-family` accepts **one** family only, no fallback stack. A family name is one Folio registered, not a filename: the faces shipped with the plugin, anything the game drops in `UI/fonts/`, or a variable face registered under its own name with axes pinned
- `font-weight` has no relative weights (`bolder`, `lighter`); use `normal`, `bold`, or 1–1000. On a variable face any value in that range is real: it drives the `wght` axis directly rather than snapping to the nearest cut. Values are quantised in steps of 50, so `375` and `425` are distinct instances and `375` and `380` are not
- `font-style` has no `oblique`
- `position: fixed` behaves like `absolute` but ignores scrolling: it is not viewport-relative
- `cursor` takes an application-defined string, not CSS keywords
- **`rgba()`'s alpha channel is 0–255, not 0–1.** `rgba(0, 0, 0, 0.5)` parses to fully transparent. Write `#0000007f` instead: hex alpha is unambiguous, and `#rgb`, `#rgba`, `#rrggbb` and `#rrggbbaa` all work. `hsl()`, `hsla()`, `lab()`, `lch()`, `oklab()`, `oklch()` and the HTML colour names are also accepted
- `z-index` takes `auto` or a number: there is no `none`
- `clip` is not the CSS `clip` property. It takes `auto`, `none`, `always` or a number, and controls how many ancestor clip regions an element ignores
- `text-overflow` takes `clip`, `ellipsis`, **or a string** to use in place of the ellipsis
- `overflow` has no `clip` value: `visible`, `hidden`, `auto`, `scroll`
- `white-space`: `normal`, `pre`, `nowrap`, `pre-wrap`, `pre-line`. `word-break`: `normal`, `break-all`, `break-word`
- `vertical-align` adds `center` to the CSS set, and `top`/`bottom` align to the line box
- `float` is `none`/`left`/`right` only, and `drag` (`none`, `drag`, `drag-drop`, `block`, `clone`) is RmlUi's own
- `tab-index` is `none` or `auto`: it is a flag, not an ordering. Tab order is document order
- percentage `row-gap` **and** `column-gap` both resolve against the containing block's *height*

### Selectors

Supported structural selectors, in full: `:nth-child()`, `:nth-last-child()`, `:nth-of-type()`, `:nth-last-of-type()`, `:first-child`, `:last-child`, `:first-of-type`, `:last-of-type`, `:only-child`, `:only-of-type`, `:empty`, `:not()`, `:scope`.

Everything else written as `:something` is treated as a **pseudo-class**: a free-form flag the host sets on an element. Unknown ones are not an error and are not warned about; they simply never match. The pseudo-classes RmlUi actually sets are `:hover`, `:active`, `:focus`, `:focus-visible`, `:checked`, `:selected` (a tab and its panel) and `:disabled`.

`:disabled` is set on **any** element with the `disabled` attribute. The attribute also writes `focus: none` onto the element as an inline property, so it leaves navigation and user clicks do not reach it. Bind it with `data-attrif-disabled="expr"`, which adds and removes the attribute; `data-attr-disabled` writes the value as text, and an attribute reading `false` is still there and still disables. The `.is-disabled` class means the same in the component library, for markup that would rather carry a class.

---

## 3. Layout

Flexbox is the primary tool. Absolute positioning is for anchoring a region, not for laying out content within one.

**In a column flex container, `flex: 1 1 0` collapses the item.** A basis of `0` makes the item's hypothetical main size zero, and in a container with no definite height that becomes the container's height too, so a panel shrinks to its header and the body's content spills out below it. Use `flex: 1 1 auto` for vertical fills. `flex: 1 1 0` is correct on the horizontal axis, where it is how one cell absorbs the leftover width of a row.

**A circle is a square box with `border-radius: 50%`, and nothing else may touch its size.** Padding or an automatic width make the box wider than it is tall, and a lozenge that is nearly a circle reads as a bug. Centre the text with `line-height`. A border is fine and gives you a ring, but only because `patches/RmlUi/0003-border-radius-clamp-against-border-box.patch` scales overlapping radii against the border box as CSS specifies; stock RmlUi scales against the box inside the borders and can never round a bordered element past `size / 2 - border`. See `rcss/recipes/circle.rml`.

**An `overflow: auto` container needs its scrollbar sized, or it destroys its own layout.** RmlUi ships no scrollbar styling, and an auto width on a scrollbar resolves to the container's full content width. The container lays out correctly right up until its content actually overflows, at which point the scrollbar takes everything and every child collapses to its own padding and border, with text inside anything `overflow: hidden` disappearing entirely. `folio.rcss` sets the geometry so this cannot happen. Do not override `scrollbarvertical`'s width with `auto`.

Use `dp` rather than `px` for anything that should scale with UI scale settings. Reserve `px` for hairlines and values that must not scale.

The complete unit set: `px` `dp` `%` `em` `rem` `vw` `vh` `in` `cm` `mm` `pt` `pc`, `deg` and `rad` for angles, `x` for resolution, and unitless numbers. There is no `ch`, `ex`, `fr`, `vmin` or `vmax`. A number written without a unit where a length is expected is a parse warning, not an implied `px`: the one exception is `line-height`, which is unitless by design.

---

## 4. Animation

**Animate `transform`, `opacity`, `color`, `background-color`, `border-color` and `filter`.** These do not trigger reflow.

**Never animate `width`, `height`, `margin`, `padding`, `top`/`right`/`bottom`/`left`, `font-size` or `line-height`.** Each triggers layout, and a HUD animating these every frame will be measurably slower than the UMG implementation it replaced.

To grow or shrink something, animate `transform: scaleX()` on an element with a fixed box. To slide something, animate `transform: translate()`, not `left`.

`transition` fires only when a class or pseudo-class changes. For continuous or self-driving motion, use `@keyframes` with `animation`.

3D transforms work: `perspective` (the property on an ancestor, with `perspective-origin`, or `perspective()` first in a `transform`) with `rotateX`, `rotateY` and `translateZ`, alongside the 2D `translate`, `scale`, `rotate` and `skew`. A tilted element is projected point by point and subdivided where its depth varies, so images, gradients and masks stay straight. Each tilted element costs a little CPU per frame, more the larger and steeper it is; keep it well short of edge-on, since past 90 degrees there is nothing to see.

**Times are seconds, and `ms` does not exist.** `transition: opacity 120ms` does not warn and does not animate: the parser reads `120` as a *reverse adjustment factor* and the transition ends up with no duration. Always write `0.12s`.

**The easing names are RmlUi's, not CSS's.** There is no `ease`, `ease-in`, `ease-out`, `ease-in-out`, `linear`, `step-*` or `cubic-bezier()`. The set is `<family>-in`, `<family>-out` or `<family>-in-out` for each of `back`, `bounce`, `circular`, `cubic`, `elastic`, `exponential`, `linear`, `quadratic`, `quartic`, `quintic`, `sine`. A misspelled easing name is read as a property name instead, which silently produces a transition that never fires.

```css
.btn { transition: background-color 0.12s cubic-out, transform 0.12s cubic-out; }
```

**A percentage inside an animated `transform` is resolved once, when the animation starts.** On a document's initial style that is before the first layout, so `translateY(100%)` resolves against a zero box, both keys collapse to `translateY(0px)`, and the animation runs forever without moving. Nothing is logged. Write the distance in `dp`. `transition` is unaffected.

`transition: all 0.12s` is legal but transitions every animatable property including layout ones. Name the properties.

---

## 5. Decorators, filters and effects

### The complete effect vocabulary

Anything not on these lists does not exist. A misspelled name is a warning, never an error, and produces no output.

**A source may name a texture the game registered** rather than a sprite or a file: `folio://minimap`. That is the seam for a scene capture, a paper doll or a rendered item - anything that is a `UTexture` at the point of use rather than something on disk. It works anywhere a source does. `uipreview` draws `UI/art/preview/<name>.png` if it exists and a placeholder otherwise. See [docs/images.md](docs/images.md).

**`decorator` and `mask-image`** take: `image`, `ninepatch`, `tiled-horizontal`, `tiled-vertical`, `tiled-box`, `text`, `shader`, `gradient`, `horizontal-gradient`, `vertical-gradient`, `linear-gradient`, `repeating-linear-gradient`, `radial-gradient`, `repeating-radial-gradient`, `conic-gradient`, `repeating-conic-gradient`, and Folio's own `dashed-border` and `dotted-border`. `@decorator` names a reusable one.

**Multiple decorators paint in reverse source order** - the first named is on top, exactly like CSS `background-image` layers. Written the intuitive way round, an effect listed after an opaque background is painted underneath it and appears to do nothing.

**`filter` and `backdrop-filter`** take: `blur`, `drop-shadow`, `brightness`, `contrast`, `grayscale`, `invert`, `opacity`, `saturate`, `sepia`, `hue-rotate`.

**`backdrop-filter` additionally takes `outside-blur`, which is Folio's own.** Where `blur` softens what is behind an element, `outside-blur` softens everything the element is not - the modal scrim, where the screen goes soft and the dialog stays sharp:

```css
.dialog { backdrop-filter: outside-blur(8dp); }
```

Two limits. The hole is a rectangle, so a `border-radius` dialog still cuts a square one. And it costs four blurred regions rather than one, because a rectangle cannot be cut out of another rectangle.

**`font-effect`** takes: `glow`, `outline`, `shadow`, `blur`, and Folio's own `fill-image`. These are the only way to get a text shadow; `text-shadow` does not exist.

### Dashed and dotted borders

`border-style` does not exist, and RmlUi's borders are solid quads with no seam to cut them at. These are decorators instead, and they stroke the element's own rounded rectangle:

```css
.drop-zone { border-radius: 8dp; decorator: dashed-border(#4ea1ff 2dp 10dp 6dp); }
.ghost     { border-radius: 8dp; decorator: dotted-border(#8b949e 1dp); }
```

`dashed-border(<colour> <width> <dash> <gap>)`, `dotted-border(<colour> <width>)` - a dot is a dash the width of the stroke.

Three things follow from it being a decorator rather than a border:

- **It draws in addition to `border`, not instead of it.** Set `border: none` on anything wearing one, or you get both.
- **It takes no space.** A real border pushes the content in; this is painted over the border box and changes no layout. Add the padding yourself if you want the same look.
- **One width for the whole outline**, since a corner between two different widths has no defined shape. Per-edge `border-width` is unaffected and still applies to the real border.

The dash period is stretched so a whole number of them fits the perimeter exactly, which is what keeps the seam invisible where the path closes. Ask for `10dp` dashes and you get very close to `10dp`, not exactly. Corners are followed as true arcs, and radii too large for their edge are shrunk to fit the same way `border-radius` itself is.

### The shader decorator

`decorator: shader("...")` reaches the effects RCSS has no syntax for, additive blending first among them:

```css
.hit    { decorator: shader("additive #ff5028c0"), image(panel); }
.crt    { decorator: shader("scanlines #7fe8ff90 0.16 0.5"), image(panel); }
.sheen  { decorator: shader("sweep #ffffffb0 0.35 0.3 15"), image(panel); }
```

- `additive <colour>` - a flat wash that adds instead of covering. Hit flashes, charge glows.
- `scanlines <colour> <period> <thickness>` - period and thickness as fractions of the box.
- `sweep <colour> <position> <width> <angle>` - a soft band across the box, angle in degrees.

`additive` and `sweep` add rather than cover. There is no blend state involved: RmlUi composites premultiplied, so a colour written with zero alpha adds to what is under it, and that works identically in both renderers.

Each is evaluated into a small texture rather than run as a fragment shader, because Slate has no seam for one through `MakeCustomVerts` and none of these needs it. The consequence is that the set is fixed - a name Folio does not know warns once and draws nothing.

### Renderer support

Every one of these depends on a render-interface feature: layers, clip masks, filter and shader compilation. `uipreview` implements the complete set. **The Unreal renderer does not yet**, so some preview correctly and render as nothing in engine:

| | In engine |
|---|---|
| Every `*-gradient` decorator, `image`, `ninepatch`, `tiled-*` | Works. The `repeat` fit modes sample wrapped, which Folio asks Slate for with a batch draw flag; they cannot name a sprite in either renderer, since a sub-rectangle of an atlas has nothing to wrap against |
| `border-radius`, borders, `transform`, `opacity` on an element | Works |
| `box-shadow`, clip masks, render layers, `filter: blur` | Works. Layers are render targets and clip masks are a real stencil, so an inverse mask - which is what stops an outer shadow painting inside its own element - is exact |
| `backdrop-filter: blur` | Works, and is the one effect that reaches outside the UI: it blurs the backbuffer, so the 3D scene behind a HUD panel blurs with it. The blurred region is a rectangle, so a `border-radius` corner blurs square |
| `backdrop-filter: outside-blur` | Works. Folio's own; see below |
| Text, at any size or transform scale | Works. See §6 |
| `font-effect`, all four | Works, in both renderers, resolved from the field. Bounded in reach - see §6 |
| `filter`: `brightness`, `contrast`, `grayscale`, `invert`, `opacity`, `saturate`, `sepia`, `hue-rotate` | Works. Applied to the resolved layer in the order named, before any blur |
| `mask-image` | Works. The masked subtree and the mask decorator are each drawn into a layer and composited, so text under a mask goes through the layer path rather than straight to the target. `examples/mask_image.rml` covers every decorator that can be a mask |
| **`backdrop-filter` other than `blur` and `outside-blur`** | **Banned.** These parse, log once and render nothing in engine |

A surface drawn through a layer lands about 3/255 darker than the same colour drawn directly, from the 8-bit sRGB round trip through the render target. Visible only side by side, and not worth a wider target format.

`decorator: shader(...)` reaches additive blending, scanlines and sweeps. See §6.

### `@media` features

`width`, `min-width`, `max-width`, `height`, `min-height`, `max-height`, `aspect-ratio`, `min-aspect-ratio`, `max-aspect-ratio`, `resolution`, `min-resolution`, `max-resolution`, `orientation` (`landscape`/`portrait`), `theme`.

**Resolution is `resolution`, not `dp-ratio`**, and its unit is `x`:

```css
@media (min-resolution: 2x) { ... }
```

Features chain with `and`, and a single leading `not` may negate the whole query:

```css
@media not (orientation: portrait) and (min-width: 1280px) { ... }
```

There are no media *types* (`screen`, `print`) and no comma-separated query lists: a comma is a parse warning, not an alternative.

---

## 6. Text

Folio replaces RmlUi's font engine outright. Glyphs are **MTSDF**: three channels of multi-channel distance field, whose median reconstructs sharp corners, plus a true signed distance in alpha that the effects read.

**Type scales freely.** Glyphs are generated once per face into a size-independent atlas, so a `font-size` of 13dp and one of 96dp cost the same atlas and both stay sharp, as does the same text under `transform: scale`, at 4K, or at any DPI. Pick sizes for the design, not for what an atlas was baked at.

The one thing to watch is the opposite of the old worry: distance-field text has no hinting and no stem snapping, so at 12–14dp on a 1080p display stems are slightly less uniform than a hinted rasteriser would give. Check small body text on the target display before committing to it.

**Advances are unhinted and scaled linearly from design units.** That is what lets one atlas serve every size, and it means text wraps in slightly different places than a hinted rasteriser would. It also means line breaks do not survive a change of dp ratio: an advance is a whole number of pixels at each size, so doubling the ratio halves the relative rounding error and words move. Never size a container to fit its content exactly at one ratio.

**Size type in `rem`, and put the one absolute size on `body`.** `rem` resolves against the document's own font size, which is what the accessibility text scale writes - so type in `rem` follows that setting and type in `dp` does not. The shipped theme is written that way, with a single `body { font-size: 20dp }` above it. See §15.

### Variable fonts

`font-weight: 1..1000` drives the `wght` axis directly, quantised in steps of 50. `opsz`, where a face has it, is driven automatically from the requested size. Other axes, `wdth` and anything custom, are pinned at registration in C++ and reached by family name:

```cpp
// On the font engine, which the module owns. Style and the pinned axes are both explicit, because a
// family is what RCSS can name and everything else has to be baked into one.
FFolioModule::Get().GetFontEngine()->RegisterVariableFace(
    "<path>", "Inter Condensed", Rml::Style::FontStyle::Normal, {{"wdth", 75.0f}});
```

```css
.narrow { font-family: Inter Condensed; }
```

### Font effects

All four are resolved from the field's true-distance channel rather than baked into extra glyph textures, so each costs one more quad per glyph and no atlas at all.

**Their reach is bounded, and the bound is in em, not pixels**: currently 0.104em at the default atlas settings. `outline(2px)` is a twentieth of an em on 40dp text and well inside it; on 13dp text it is a sixth of an em and gets clamped, with a warning naming the limit. Size an effect against the text it decorates, and check small text specifically.

The bound is a little under half the field's range rather than exactly half. Past half, the field is saturated: a texel an em away from the glyph holds the same number as one just beyond the edge, so an effect whose edge lands there has no outside to fade into. The last texel is held back to give it one.

Clamping is a real limit, not a soft one. An effect asked for at three times the cap looks identical to one asked for at the cap. To go wider, raise `pxrange` when baking, at a quadratic cost in atlas area.

### Textured text

`fill-image` fills the glyphs with a pattern instead of a flat colour - a scanline, a gradient, a gold-leaf sheet:

```css
.crt { font-effect: fill-image("/UI/art/scanlines.png" 4dp); }
```

The arguments are a path, a tile size, and an optional colour the pattern is multiplied by. Three things to know:

- **It names a file, not a sprite.** A font effect is built while the stylesheet is parsed, and has no route to the spritesheet a decorator would use.
- **The pattern is anchored to the screen, not to the text.** A scanline stays put while text scrolls under it, which is what a scanline is for and wrong for anything meant to travel with the words.
- **It only textures the fill.** It is a front layer over the glyph, so an `outline`, `glow` or `shadow` on the same text stays flat. Where the pattern is transparent the flat `color` shows through, so the two compose.

Not available inside a layer - text under a `filter`, a `mask-image`, or an `opacity` on an ancestor. Those paths bind one texture, and the pattern is skipped.

---

## 7. Vector sprites

An icon can come from an SVG instead of a PNG, baked to the same kind of distance field the text uses. `Tools/msdfbake vector` reads the SVG, packs the fields into an atlas and writes a `@spritesheet` block. Markup does not change: `<img sprite="icon_sword">` and `decorator: image(icon_sword)` work exactly as they do for raster sprites.

Use it for art that has to survive extreme magnification: a HUD icon that also appears on a full-screen map, anything on a world-space surface the player can walk up to. Everything else stays raster; the vector path costs a draw per colour.

The rules, all of which come from what a distance field can represent:

- **A field is coverage and nothing else.** One field is one silhouette. Multi-colour art is baked as a **stack of silhouettes**, one per fill colour, painted back to front - still one atlas, still resolution independent, at the cost of one draw per layer. Keep the colour count low deliberately.
- **A stack needs its frame image.** Every layer is absolutely positioned and so adds nothing to its container's height; on its own the container collapses and nothing renders. The baker emits a `<name>_frame` sprite carrying the drawing's proportions, and a hidden `<img>` on it is what gives the box a height:

  ```html
  <div class="knight-stack" style="width: 240dp">
    <img class="knight-frame" sprite="knight_frame"/>
    <img class="knight_0-ink" sprite="knight_0"/>
    <img class="knight_1-ink" sprite="knight_1"/>
  </div>
  ```

  Set the width and let the frame find the height. RCSS has no `aspect-ratio`, and an image element's intrinsic size is the only thing that carries one.
- **`ninepatch` and `tiled-box` are invalid on a vector atlas.** Non-uniform stretching corrupts the distance metric, so corners round off and edges soften. A vector sprite scales uniformly or not at all.
- **Set width or height, not both.** They resolve against different bases - the parent's width and the parent's height - so a stack sized as a percentage of both squashes whenever the window aspect changes. One dimension preserves the intrinsic ratio.
- The vector sheet is declared `1x`. There is no second resolution to author.
- **`clip-path` is honoured.** nanosvg discards it, so the baker reads the relationship out of the SVG itself and intersects the geometry with Skia's path operations - the same library that unions a glyph's overlapping contours. A clip path's own shape is a stencil and is never drawn, which is what SVG says and what nanosvg gets wrong.
- **Strokes are outlined into area**, since a field has no notion of a line. Joins (miter with its limit, round, bevel) and caps (butt, round, square) are read from the SVG and honoured. A stroke costs its own layer, because it is its own colour.

**Baking is not automatic.** Re-run `msdfbake vector` after changing an SVG.

RmlUi caches a texture by its path and never looks at the file again, so a re-baked atlas would otherwise keep rendering as it was. Reopening a document picks up anything whose timestamp has moved; `folio.ReloadTextures` does it without reopening, and `folio.ReloadTextures all` releases every texture whether or not it looks changed. `uipreview` handles it through its file watcher and needs nothing.

---

## 8. Navigation

Navigation is RmlUi's built-in spatial navigation. **Do not implement custom focus logic.**

**`nav` is read from the focused element itself, and it does not inherit.** This is the single most important thing on this page. When a direction key arrives, RmlUi reads `nav-<direction>` as a *local* property of the element that currently has focus. A container's `nav` is never consulted, and `nav` set on an ancestor is never inherited down. Verified directly: three `tab-index: auto` items inside a `.list { nav: vertical; }` container navigate exactly once, into the first item, and then stop dead.

```css
/* Does nothing. */
.menu { nav: vertical; }

/* Correct: the property goes on the focusable items. */
.menu-item { tab-index: auto; nav: vertical; }
```

So: **every focusable element carries its own `nav`**, declared next to its `tab-index: auto` in whatever rule defines the component.

The rest:

- `nav: auto` on `body`. The document is the focus holder before anything else is focused and after focus is lost, and this is what lets the first direction press enter the content. Without it a fresh document is unreachable by controller. It is also the recovery path after a mouse click lands on nothing
- Values are `none`, `auto`, `horizontal`, `vertical`, or an `#id` string. `horizontal` and `vertical` are filters on the *incoming direction*, not on where the search looks: `nav-up: vertical` passes (up is a vertical direction), `nav-up: horizontal` blocks. So the useful form is the single value: `nav: vertical` on a menu item means up and down navigate and left and right do nothing at all
- `nav` is a **box shorthand** in `up right down left` order, exactly like `margin`. Which makes `nav: vertical horizontal` a trap: it reads as up/down `vertical` and left/right `horizontal`, every one of which passes its own filter, so it is an elaborate way of writing `nav: auto`
- Axis locking keeps focus from jumping sideways into unrelated chrome, but it also stops focus *leaving*. A dialog's action row wants `nav: auto` so up returns to the content; a long settings list wants `nav: vertical` so a stray left press does not land in the sidebar. Choose per component, not globally
- `nav-down: #element_id` to override when the spatial heuristic picks wrong. A missing id warns at load
- **Both ends must agree.** Navigation is resolved entirely from the source element, so `a { nav-down: #b }` does not give you `b { nav-up: #a }`. Explicit overrides come in pairs
- `tab-index: auto` makes an element focusable, and nothing else does. It is a flag, not an order: Tab walks document order
- `focus: none` makes an element and its whole subtree unfocusable, and it inherits. It also suppresses `click`, because the click is dispatched to the nearest *focusable* ancestor rather than to what was under the cursor, so a `focus: none` element silently forwards its clicks to whatever contains it. `mousedown`, `mouseup`, `mouseover` and `:hover` still fire on it. `pointer-events: none` is what takes it out of hit testing entirely. Disabled controls want all three: a class for the look, `focus: none` to leave navigation, `pointer-events: none` to stop responding
- Enter, numpad Enter and Space on a focused element with `tab-index: auto` synthesise a click. You get keyboard and gamepad activation for free, and you should not add a separate keydown path for it
- The spatial search starts from the focused element's **nearest scroll container**, and any element whose `overflow` is not `visible` on either axis is a scroll container. An incidental `overflow: hidden` on a layout wrapper will therefore confine focus somewhere you did not intend, and it reads as a navigation bug rather than a styling one. If focus refuses to leave a region, look for `overflow` first
- **A modal is only half-trapped by that rule.** `overflow: hidden` keeps navigation inside the dialog once focus is already there, but nothing puts it there. A fresh document has nothing focused, and the first direction press runs the *document's* `nav`, which takes the first tabbable element in document order: the content behind the dialog. Make the background inert (`focus: none`, which inherits down the whole subtree, plus `pointer-events: none`) for as long as the dialog is up. Verified: without it a scripted `down,right,up,left` walks the buttons behind the dialog and never reaches it

Style the focus state with `:focus-visible`, never `:focus`. `:focus-visible` is set by navigation actions specifically, so the focus ring correctly stays hidden during mouse use.

### What counts as a direction

Everything below arrives as the same four keys, and all of it is live at once. A document never learns what produced a direction.

| | |
|---|---|
| Arrow keys, **WASD**, **IJKL** | up, down, left, right |
| D-pad, and either stick pushed to its extreme | the same four |
| Either analog stick, held | repeats: 0.5 deadzone, 0.4s before the first repeat, 0.12s after |
| `Enter`, `Space`, gamepad **confirm** face button | activate the focused element |
| `Escape`, the other face button | back |
| `Tab`, either shoulder | tab order |

**Letters only navigate when nothing is taking text.** With an `input` or `textarea` focused, W is a W. That is what lets three keyboard schemes coexist without any of them breaking typing.

**Each scheme can be switched off** by a project, from `DefaultEngine.ini` or the console, with no rebuild: `folio.Nav.Arrows`, `folio.Nav.WASD`, `folio.Nav.IJKL`, `folio.Nav.Gamepad`. A game that binds WASD to movement while a HUD is up wants that one off. Turning the arrows off still leaves them working inside a text field, where they mean something else.

### Binding your own keys

Nothing has to be bound for a document to navigate: the schemes above cover keyboard and pad out of the box. For a game that wants its own, or a rebinding screen, keys map to **actions**: `Up`, `Down`, `Left`, `Right`, `Confirm`, `Back`, `NextTab`, `PreviousTab`.

A `UDataTable` of `FFolioNavBinding` rows (`Key`, `Action`) is the authored form:

```cpp
UFolioNavLibrary::LoadNavBindings(Table, /*bReplaceDefaults*/ true);
```

With `bReplaceDefaults` the table becomes the **entire** mapping and no built-in scheme applies. Without it the rows are additions, so arrows and the pad keep working alongside them.

One key at a time, which is what a rebinding screen does:

```cpp
UFolioNavLibrary::SetNavBinding(EKeys::Q, EFolioNavAction::Left);
UFolioNavLibrary::GetKeysForNavAction(EFolioNavAction::Left);  // to show the player
```

**A binding wins over the built-in schemes and ignores the `folio.Nav.*` switches**: a project that bound a key meant it. The confirm/back swap applies only to the *default* gamepad face buttons; a table naming `Confirm` has already said which button it means.

All of it is reachable from Blueprint, and from the console for trying one out: `folio.Nav.Bind Q Left`, `folio.Nav.Bindings`, `folio.Nav.ResetBindings`.

**UI sounds** are reported, not played: `FFolioModule::OnUiSound()` fires `Focus`, `Confirm`, `Back` or `Invalid`. Folio owns no sound assets. `Invalid` is a click on something carrying `.is-disabled`, which is the case worth a different noise.

### Putting a document inside a game's existing widget tree

`UFolioWidget` is a UMG widget wrapping one document. It exists for the boundary, not for building anything: a game keeps the HUD layout, CommonUI stack or menu flow it already has and puts a document inside it, rather than porting the shell before the first screen exists.

```cpp
UPROPERTY(meta = (BindWidget))
TObjectPtr<UFolioWidget> Document;
```

**If the project uses CommonUI, return it as the focus target.** CommonUI's action router owns focus. When it reapplies its input config (which `SetInputMode` and any gamepad press both cause) it asks its leaf-most activatable widget for a target, and if it gets none it can use it **focuses the game viewport instead**, silently taking focus off a document that had it. The symptom is keyboard working and the gamepad doing nothing at all, with no error logged. Look for:

```
LogUIActionRouter: [User 0] No focus target for leaf-most node [...], and the widget
                   isn't focusable - focusing the game viewport.
```

One override fixes it for good:

```cpp
UWidget* UMyScreen::GetDesiredFocusTarget() const override { return Document; }
```

That works because `UWidget::SupportsKeyboardFocus` defers to the Slate widget underneath, and the canvas supports focus. **Folio links no CommonUI** and behaves the same in a project that has never heard of it: the integration is entirely on the game's side of the seam.

`SetKeepFocus(true)` exists for documents with no activatable widget to hang that override on, and reclaims focus whenever input arrives. Prefer the override: `SetKeepFocus` wins the argument, rather than avoiding it.

### Which face button confirms

South (A, Cross) confirms by default and east (B, Circle) goes back. Nintendo hardware and Japanese conventions are the other way round, so it is a setting, not a constant:

```cpp
FFolioModule::Get().SetConfirmButton(Folio::EConfirmButton::East);
```

or `folio.Nav.SwapConfirmBack 1` from the console or a per-platform `DefaultEngine.ini`. Changing it swaps what the two buttons *do* and updates every open document in the same frame. **Folio has no platform opinion of its own**: it does not check what it is running on, because the answer depends on region as much as hardware. A port decides and calls the setter.

### Telling a document what the player is holding

Folio tracks the last device used (keyboard, mouse or gamepad) process-wide rather than per document, so a HUD and a pause menu open together cannot disagree. It reaches markup two ways, and most UI only needs the first.

**Body classes**, for anything that is presentation:

| | |
|---|---|
| `folio-device-keyboard` `folio-device-mouse` `folio-device-gamepad` | exactly one, the precise device |
| `folio-input-kbm` `folio-input-gamepad` | exactly one, the coarse question glyph swapping actually asks |
| `folio-confirm-south` `folio-confirm-east` | exactly one, which button confirms |

```css
.prompt { display: none; }
body.folio-input-gamepad .prompt.pad { display: inline-block; }
body.folio-input-kbm     .prompt.key { display: inline-block; }

/* A focus ring is noise under a mouse and essential without one. */
body.folio-device-mouse .menu-item:focus-visible { border-color: transparent; }
```

`folio-input-*` is redundant with `folio-device-*` on purpose: keyboard and mouse are both "not a gamepad", and without the coarse pair every rule needs a duplicate selector or `:not()`.

**The reserved `folio` data model**, for the rest: when an expression has to *build* a value:

```html
<div data-model="folio">
	<img data-attr-sprite="'btn_' + confirm"/>
	<span data-if="gamepad">hold to skip</span>
</div>
```

| field | |
|---|---|
| `device` | `keyboard`, `mouse` or `gamepad` |
| `gamepad` | bool, the same coarse question |
| `confirm` | `south` or `east` |
| `back` | always the other one |

`folio` is **a reserved model name: do not use it for game data.** It is mounted on every context whether a document binds it or not, so a game that never touches it pays one unused model. An element binds one model at a time, so a screen with its own model reads these from a nested subtree, as above, rather than from `<body>`.

Both routes are pinned in the preview with `--input-device keyboard|mouse|gamepad` and `--confirm-button south|east`, which is how a gamepad screenshot is taken without a gamepad.

From C++, `FFolioModule::Get().GetInputDevice()` and `OnInputDeviceChanged()`.

---

## 9. Data binding

There is **no JavaScript**. Interactivity is expressed three ways: data bindings for anything driven by game state, state classes and pseudo-classes for appearance, and named events with C++ listeners for actions.

`data-model` mounts a model on an element. Inside it:

| Attribute | What |
|---|---|
| `data-text` (or `{{ expr }}` in text) | Substitutes a value into text content |
| `data-attr-<name>` | Sets an attribute |
| `data-attrif-<name>` | Sets a valueless attribute when truthy |
| `data-class-<name>` | Toggles a class |
| `data-style-<property>` | Sets a style property. Avoid: it writes an inline style, which a stylesheet-only reload will not touch |
| `data-if` / `data-visible` | `data-if` adds and removes the element; `data-visible` only toggles `visibility`, so it keeps its box |
| `data-for` | Repeats an element over an array |
| `data-alias-<name>` | Renames a binding inside a `data-for` |
| `data-rml` | Substitutes markup rather than text. Do not use it on anything a player can influence |
| `data-value`, `data-checked` | Two-way binding for form controls |
| `data-event-<name>` | Runs a data expression on an event |

**There is no `data-else`** in 6.2; write the complementary `data-if`.

`{{ }}` interpolation is text-only and only inside an element's text content, not in attribute values, where `data-attr-*` is the mechanism.

**`{{ items.size }}` gives an array's length**, and works anywhere an expression does - `data-if="items.size == 0"` for an empty-list message, `{{ items.size }} items` for a count.

This is Folio's, not RmlUi's. Upstream calls `DataVariable::Size()` from `data-for` and nowhere else, so nothing in an expression can reach a length; a transform function cannot help either, because transforms take Variants and an array variable cannot produce one. Both data models answer a `size` member on an array instead. An array has no named members of its own, so nothing is shadowed.

Inside a `data-for`, `it_index` is the zero-based index and is always available.

### Several models in one document

A document may mount as many models as it has been bound, and **a `data-model` inside another model's subtree is supported**: the inner element takes the named model and the outer model stops descending there. So a `<body data-model="menu">` holding a `<div data-model="bindings">` is a correct shape, and an empty list is never that nesting's fault.

Bind every model **before the document loads**. Binding one afterwards reloads the document, which is reported and costs a rebuild.

### Fill an array before the document loads

A `data-for` builds its rows when the document resolves the model. An array filled after that only reaches the list through the model's per-frame poll, so a model that is bound empty and filled on open is a list that arrives a frame late at best and never at worst. Fill it where you create it.

### Every list gets an empty state

    <div class="row" data-for="item : Items">...</div>
    <div class="none" data-if="Items.size == 0">Nothing here yet.</div>

Without the second line an empty array and markup that failed to resolve are the same blank rectangle, and they are fixed in completely different places.

---

## 10. Stylesheet composition

**RCSS has no `@import`.** A stylesheet cannot pull in another one. There are exactly two ways to get rules into a document:

```html
<link type="text/rcss" href="/UI/lib/button.rcss"/>   <!-- one per stylesheet -->
<style> /* document-local rules */ </style>
```

Listing eight component stylesheets in every document is how this becomes unmaintainable, so use the one aggregation mechanism RmlUi does have: **a template's head is merged into any document that links it.**

```html
<link type="text/template" href="/UI/lib/lib.rml"/>
```

A template whose `<head>` links every component stylesheet gives a document the whole library in one line. The document does not need to *use* the template: merely linking it merges the head. Verified directly.

Order: template heads merge first, then the document's own head, so a document's `<style>` block always wins over the library. Within the library, later `<link>`s win at equal specificity.

---

## 11. Element vocabulary

RML is not HTML. Only these tags are backed by real element types with behaviour:

| Tag | What it does |
|---|---|
| `body` | The document element |
| `img` | Image. Takes `sprite="name"`, or `src` + `rect` |
| `handle` | Drag/resize handle. `move_target="id"` moves that element, `size_target="id"` resizes it, `edge_margin` keeps it inside its parent. Both are RmlUi's own and need no code |
| `form`, `input`, `select`, `textarea`, `label` | Form controls. `input` types: `text`, `password`, `checkbox`, `radio`, `range`, `submit` |
| `progress` / `progressbar` | Takes `value`, `max`, `direction` (`top`, `right`, `bottom`, `left`, `clockwise`, `counter-clockwise`) and `start-edge`, plus the `fill-image` property |
| `tabset` | `<tabset><tabs><tab>…</tab></tabs><panels><panel>…</panel></panels></tabset>`. Sets `:selected` on the active tab and its panel, and `display: none` on the others, as inline properties |
| `text` | A paragraph of prose whose contents are Folio's markup rather than RML: `<style>x</>`, `{tag:}`, `{delay:}`. Reveals one character at a time under `reveal: typewriter`. See [rich text](docs/rich_text.md) |

**`<text>` is read raw, and closes as RML.** Its contents are never parsed as elements, which is what lets a translated string carry markup. The element itself still closes with `</text>`; the `</>` shorthand closes a style *inside* it. It holds prose and nothing else - no `<img>`, no binding, no `{{ }}`, because the braces belong to the markup.

**A tabset silently grows a second, empty `tabs` and `panels` pair.** It creates either child on demand whenever it looks for one and does not find it, and during parsing it looks before the markup's own children are in place, so a tabset that declares both still ends up with four children: the real pair, then an empty pair appended after them. Everything still *works*, because it always takes the first match. But a bare `tabs` or `panels` tag selector styles two elements, and any border or background on it is drawn a second time at the bottom of the tabset. It looks like a layout bug in the panel. Always write `tabs:first-of-type` and `panels:first-of-type`.

**Every other tag is a generic element.** `div`, `p`, `h1`, `span`, `button`, `ul`, `li`, `section`, `nav` and anything else you invent all instance the same class and differ only in what the stylesheet says about them. That is a feature: a `<menuitem>` tag is as real as a `<div>`, and reads better. It also means `<button>` is not a button: it has no focus behaviour and no click handling until it gets `tab-index: auto`.

Tag names are matched literally in selectors, so keep the invented ones documented.

---

## 12. Dragging and dropping

`drag` on an element decides whether it can be picked up at all: `none` (the default), `drag`, `drag-drop`, `block` or `clone`. Without it a press is a press and nothing follows the pointer.

**Moving and resizing need no code.** `<handle>` is a real element type and does both:

```html
<div id="win" class="window">
	<handle class="titlebar" move_target="win">Title</handle>
	<div class="body">…</div>
	<handle class="grip" size_target="win"></handle>
</div>
```

The target is named by id, so a handle can move or resize anything, not only its parent. The window must be `position: absolute`: a handle writes `left`, `top`, `width` and `height`, and those mean nothing to a flex item. `edge_margin` on the handle keeps the target inside its parent.

**`drag` fires events; it moves nothing.** RmlUi reports the gesture and leaves the element where it was. `folio-drag="move"` is what actually repositions it, and the element must be `position: absolute` or the `left` and `top` it writes mean nothing. It warns if you forget.

**Use `drag-drop`, not `drag`, on anything you mean to drop.** RmlUi only looks for a drop target when the dragged element's `drag` is `drag-drop` or `clone`; with plain `drag` the zone never hears about it and `folio-drop` never runs.

Folio adds:

| | |
|---|---|
| `folio-dragging` | Class, set on the dragged element for the length of the drag |
| `folio-dragover` | Class, set on a drop zone while a drag is over it |
| `folio-drop="hide"` | Attribute on a drop zone: hides the dropped element |
| `folio-drop="remove"` | Removes it from the document |
| `folio-drop="accept"` | Does nothing itself, but still gets `folio-dragover` for styling |
| `folio-drag="move"` | The element follows the pointer while dragged |
| `folio-close="id"` | On click, closes that element. No id closes the parent |
| `folio-closing` | Class, set on anything on its way out, by a close **or** a drop. Animate it and the removal waits for the animation to finish |

Only an element with `folio-drop` gets `folio-dragover`, so an inert element never offers to accept a drop it will not take.

**The attribute is looked for up the tree, not on the event's target.** An event targets the deepest element under the pointer, so dropping on a bin means dropping on the bin's icon or its label. Put `folio-drop` and `folio-close` on the box you think of as the thing; children need nothing.

**`hide` and `remove` animate too.** Both put `folio-closing` on the dropped element and wait, the same as a close, so an icon can shrink into the bin before it goes.

```html
<div class="icon" drag="drag-drop" folio-drag="move">CORE.DAT</div>
<div class="bin" folio-drop="hide">Recycle</div>

<div id="win" class="window">
	<handle class="titlebar" move_target="win">Title</handle>
	<div class="close" folio-close="win">X</div>
	<handle class="grip" size_target="win"></handle>
</div>
```

```css
.icon { position: absolute; }
.icon.folio-dragging { opacity: 0.55; }
.bin.folio-dragover { border-color: #fff; }

/* Closing waits for this. Without an animation the element hides at once. */
@keyframes suck { to { transform: translateY(520dp) scale(0.04); opacity: 0; } }
.window.folio-closing { animation: 0.5s cubic-in suck; }
```

Anything past those three actions is the game's: listen for `dragdrop` in C++, or use `data-event-dragdrop` against a bound model. This exists so the common case needs neither, and so both renderers behave the same: the listener lives in `FolioCore`, which the preview tool and the Unreal host both install.

**A drag is a pointer gesture.** It works on a world-space document exactly as on screen, because both feed the same press, move and release.

---

---

## 13. Themes

A theme is `@media (theme: name)` in a stylesheet, turned on with `folio.Theme <name>` or **Set Theme** from Blueprint, and `uipreview --theme <name>` previews one.

**A theme is an override block, not a swapped value.** RCSS has no custom properties and no `var()`, so a stylesheet cannot hold a variable a theme replaces. It holds both looks, and the later rule wins. Which is why a component may not contain a colour: a colour in the component's own rule can only be overridden by restating that rule in full, and the first game to disagree forks the library.

So the split is:

| | |
|---|---|
| `/folio/lib/*.rcss` | Structure. No colour, anywhere. |
| `/folio/themes/default.rcss` | The tokens, and a skin for every library class. |
| the game's own stylesheet | Anything it disagrees with, and anything naming the game. |

Load them in that order. A game's rules land last and win by being later.

**Several themes can be on at once.** They are independent switches, not a list to pick from, so `colour-blind` and `large-text` compose without either stylesheet knowing about the other.

**A theme is global, not per document.** RmlUi tracks it per context, so Folio walks every context when one changes and remembers it for contexts created later - a screen opened after the switch comes up in the new look rather than the old one.

**Two themes are accessibility settings rather than looks**, and a game turns them on by name rather than by theme string - `SetHighContrast`, `SetReducedMotion`.

`high-contrast` ships in the default theme. It widens the focus ring as well as brightening it, and stops disabled controls relying on opacity alone. A screen that writes its own colours has to restate them under the same query: a theme cannot reach a rule inside a document's `<style>`.

`reduced-motion` ships in `/folio/folio.rcss`, the user-agent stylesheet, so it reaches a game's own rules and elements the theme has never heard of. It is one rule - `* { transition: none; animation: none; }` - and it stops everything a stylesheet moves and nothing that a shader, a video or your own per-frame code moves.

---

## 14. Localisation

Folio's mechanism, which a game's own conventions will have policy on top of.

**A user-visible string carries a `data-loc` key:**

```html
<p data-loc="start">Start Game</p>
```

The source text stays inline so it is readable and previewable. The key makes it gatherable into Unreal's localisation pipeline.

**A `data-loc` element holds text and nothing else.** Translation replaces the element's entire contents, so a child element inside it is swallowed into the string and comes back out as escaped, visible markup: a tab reading `Accessibility<span class="tab-badge" />`. Put the key on an inner text-only element and keep the decoration as its sibling. `uipreview` reports this as `W-LOC-MARKUP`.

```html
<tab class="tab focusable"><span data-loc="accessibility">Accessibility</span><span class="tab-badge"/></tab>
```

`data-noloc` is the explicit opt-out for strings that are not language: numerals, timers, player names, version strings. It is greppable, so what has been excluded stays reviewable. Text produced by a data binding needs neither: it was already `FText` in C++ and is gathered by the normal source pass.

`uipreview --culture pseudo --strict` reports untagged literal text as `W-LOC-UNTAGGED`. Note that **`--strict` alone does not run the localisation lint**: the check only runs when a culture is loaded, so the two flags go together.

A `<text>` is the one exception to the text-and-nothing-else rule, and the reason it exists: its markup is part of the string, so emphasis and substituted names survive translation and a translator can move them onto whichever word carries them in their language. One key covers the whole line, and `gather_rml.py` writes the brief that explains the tags to whoever receives it.

Assume translated strings are up to 40% longer than English.

---

## 15. Accessibility

Six attributes, one command that fails the build if you skip them, a screen reader that reads the result, and three settings that change how a screen is presented.

```
uipreview <file.rml> --a11y --strict     # contrast, size, targets, names, focus, reachability
uipreview <file.rml> --a11y-tree         # what a screen reader is read out of, in reading order
```

The checks cover contrast against the real backdrop, text under 12dp, hit targets under 24dp, controls with no name, and controls whose focus state is invisible. Every recipe and the default theme pass with no findings, so building on the component library inherits that.

| | |
|---|---|
| `folio-label="..."` | Names a control that has no text of its own, and overrides text that reads wrong out loud. |
| `folio-role="..."` | What kind of control this is, when the tag does not say. Inferred from the tag otherwise. |
| `folio-description="..."` | A sentence read after the name, for a consequence the name cannot carry. |
| `folio-checked="true\|false"` | The state of a check box built out of `div`s. A real `<input>` needs none of this. |
| `folio-hidden` | Decoration. Skipped by the checks and by the screen-reader tree. |
| `folio-announce[="assertive"]` | A line read out whenever it changes, wherever the reader happens to be. Also how a value nobody can focus - the number between a minus and a plus - gets heard. `assertive` cuts in over whatever is being said; bare waits its turn. |
| `folio-screen-effect` | A full-screen wash or flash. Hidden when a player turns screen effects off. |
| `folio-bake-source` | On `body`: this document exists to be baked into textures, so the full-screen layer warning does not apply to it. |
| `folio-inert` | Out of reach, as the background behind a modal is. `focus: none` with `pointer-events: none` says the same thing, and is what `docs/modals.md` already tells you to write. |

A control counts as named if it has `folio-label`, its own text, text inside it, a `<label for>` pointing at it, or - for a form control - a wrapping `<label>` or a non-interactive sibling with text on either side. You only write `folio-label` for icon-only controls and for text that reads wrong - an `X` that means Close.

Do not use `folio-hidden` to silence a finding on something a person is meant to read.

While a screen reader is running, the document is walked into a tree of Slate widgets, one per thing worth announcing, and NVDA, JAWS and console TTS read that. A game can also read its own menus aloud without one - `UFolioAccessibilityLibrary::SetScreenReader`, which is what an options toggle calls. Nothing is built while neither is on. `--a11y-tree` prints the same tree the engine builds, so it is checkable without a screen reader; `folio.Accessibility.Dump` prints it from a running game.

The game's own reader also needs a **review cursor** bound to something - `ReviewNext`, `ReviewPrevious`, `ReviewRepeat`, `ReviewAll`. Focus only ever reaches the controls, so without one a player hears a menu with all of its words removed. `<h1>`-`<h6>` become headings the cursor can jump between, which is how a long screen is read without walking every line of it.

`SetReaderVerbosity` decides how much it says beyond a name: `Low` is name and state, `Normal` adds the kind of control and the panel, `High` adds the description. `SetPunctuation` is its own axis, for how much of a field's punctuation is named as it is typed; `ReadEditWord`, `ReadEditLine` and `SpellEditWord` read a field back without moving the caret.

**Use the markup that already carries structure.** `<h1>`-`<h6>`, `<ul>`/`<li>` and `<table>`/`<tr>`/`<th>`/`<td>` all infer their roles, and they buy heading navigation, "2 of 7" on a list item, and a cell read under its column's name.

**The same walk is readable from game code**, with nothing turned on: `DescribeScreen` gives every line in reading order and `GetFocusedControl` gives the one the player is standing on. Keyed by name rather than by selector, so a UI test, a telemetry hook or an NPC reacting to a menu survives the screen being rebuilt.

**Three presentation settings**, all global and all off by default:

| | |
|---|---|
| `SetTextScale(1.5)` | Writes the root font size, so it reaches type sized in `rem` and nothing sized in `dp`. **Size your type in `rem`.** |
| `SetReducedMotion(true)` | The `reduced-motion` theme, and instant scrolling. Code that moves things per frame reads `IsReducedMotionOn` and snaps. |
| `SetScreenEffects(false)` | Hides every element marked `folio-screen-effect` - the vignettes, tints and flashes painted over the whole view. Mark them; Folio cannot tell one from a panel. |
| `SetTextSpeed(2)`, `SetInstantText(true)` | How fast `<text>` types. Its own setting on purpose: players look for text speed separately, and a typewriter is not what a vestibular setting is for. |
| `SetHighContrast(true)` | The `high-contrast` theme. |

Check them the way a player would use them:

```
uipreview <file.rml> --a11y --text-scale 1.5             # what stopped fitting, what did not move
uipreview <file.rml> --a11y --theme high-contrast --a11y-contrast 7
uipreview <file.rml> --a11y --simulate deuteranopia      # and redraws every capture
```

See **[docs/accessibility.md](docs/accessibility.md)**.

---

## 16. Verifying a document

```
uipreview <file.rml> --size 1920x1080
uipreview <file.rml> --culture pseudo --strict --screenshot pseudo.png
uipreview <file.rml> --keys "down,down,right,enter" --shot-each out/
uipreview <file.rml> --json-report report.json
```

`--strict` is the one that matters, because nearly every RCSS mistake is a warning rather than an error in RmlUi and a check that only looks at errors will pass a thoroughly broken stylesheet. A non-zero exit code means the document is invalid.

### The harness

`scripts/verify.sh` runs every document in `Tools/uipreview/tests/` under `--strict` at two dp ratios, writes a metrics report for each, optionally diffs them against a previous run, and takes the screenshots that have to be judged by eye.

```bash
./scripts/verify.sh --out build\verify-before
# ...change something...
./scripts/verify.sh --baseline build\verify-before
```

`--metrics-report` is what makes a text-engine change reviewable. It records where every text element and every wrapped line landed, and the diff reports lines that broke somewhere new: the regression this system is most likely to cause and the least likely to be noticed by eye. Compare like for like: same size, same dp ratio.

`scripts/check_shader_parity.sh` fails if `Shaders/Private/FolioMsdf.ush` and the GLSL in `Tools/uipreview/src/MsdfProgram.cpp` have drifted. `build.sh` runs it before compiling. A golden-image comparison cannot catch this: the two renderers' colour pipelines never match pixel for pixel, so only the source can be compared.

`build.sh --asan` for any intermittent crash. Lifetime bugs here tend to read freed memory that is still intact, so they survive for weeks and then fail somewhere else.

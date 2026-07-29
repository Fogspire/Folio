# Folio: what RCSS actually supports

**This document is authoritative for capability.** Anything authoring `.rml` or `.rcss` against
Folio — human or model — follows it. Where it conflicts with general CSS/HTML knowledge, this
document wins.

RCSS is based on CSS2 with selected CSS3 features. It is **not** a CSS3 subset and it is **not**
a browser. The most common failure mode when writing for it is emitting valid-looking CSS that
RmlUi silently ignores.

This file describes the engine. A game layered on Folio will have its own conventions document
covering house style — token palettes, directory structure, which components to compose, when a
string must be localised. That one governs taste; this one governs what exists.

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

**Layout & box model**
`display` (inline, block, inline-block, flow-root, flex, inline-flex, table, inline-table, table-row-group, table-row, table-column-group, table-column, table-cell, none) · `position` (static, relative, absolute, fixed) · `top` `right` `bottom` `left` `inset` · `float` `clear` · `width` `height` `min-width` `max-width` `min-height` `max-height` · `margin` (+ per-edge) · `padding` (+ per-edge) · `box-sizing` · `overflow` `overflow-x` `overflow-y` · `overscroll-behavior` · `clip` · `z-index` · `vertical-align`

**Flexbox** — fully supported, and the primary layout tool
`flex` `flex-basis` `flex-direction` `flex-flow` `flex-grow` `flex-shrink` `flex-wrap` · `align-content` `align-items` `align-self` · `justify-content` · `gap` `row-gap` `column-gap`

**Colour & surface**
`color` · `background-color` · `background` (colour only) · `opacity` · `border-width` (+ per-edge) · `border-color` (+ per-edge) · `border` and `border-top`/`-right`/`-bottom`/`-left` (width + colour only) · `border-radius` (+ per-corner) · `box-shadow` · `image-color` · `decorator` · `mask-image` · `filter` · `backdrop-filter`

**Text & fonts**
`font` `font-family` `font-size` `font-style` `font-weight` `font-kerning` · `font-effect` · `text-align` `text-decoration` `text-transform` `text-overflow` · `letter-spacing` `line-height` · `white-space` `word-break`

**Animation**
`transition` · `animation` · `transform` `transform-origin` · `perspective` `perspective-origin`

**Interaction**
`nav` `nav-up` `nav-right` `nav-down` `nav-left` · `tab-index` · `focus` · `pointer-events` · `cursor` · `caret-color` · `drag` · `visibility`

**Misc**
`fill-image` (progress elements) · `scrollbar-margin`

**At-rules:** `@spritesheet`, `@keyframes`, `@media`, `@decorator`. **That is the complete set** —
there is no `@import` and no `@font-face`. See §10 for how stylesheets are composed without
`@import`.

### Defaults that differ from a browser

RmlUi has no user-agent stylesheet, so these are the raw initial values every element starts
from. Folio's `/folio/folio.rcss` fixes the first one; the rest are live.

| Property | RCSS initial value | Consequence |
|---|---|---|
| `display` | `inline` | Every element, including `div` and `p`. Without `folio.rcss` a document is one run of overlapping text |
| `color` | `white` | Not black |
| `font-size` | `12px` | Small, and in `px` — it does not scale with dp-ratio until you set it |
| `font-family` | *(empty)* | No text renders at all until a family is named |
| `line-height` | `1.2` | A unitless multiplier, and there is no `normal` |
| `background-color` | `transparent` | |
| `box-sizing` | `content-box` | Padding and border add to `width`. Most components want `border-box` |
| `tab-index` | `none` | Nothing is focusable until you say so |
| `nav-*` | `none` | Directional input does nothing at all until you say so. See §7 |
| `text-align` | `left` (inherited) | |
| `focus` | `auto` (inherited) | |

`width` on the document `body` also needs setting explicitly — a document is laid out
shrink-to-fit against the context regardless of `display`. `folio.rcss` sets `width: 100%`; add
`height: 100%` yourself if you want a full-viewport background.

---

## 2. Banned — will silently do nothing

| Instead of | Use |
|---|---|
| `display: grid` and all `grid-*` properties | flexbox |
| `var()` and `--custom-properties` | concrete classes in a token stylesheet |
| `background-image`, `background-size`, `background-position`, `background-repeat` | `decorator: image(...)` |
| `border-style` (`solid`, `dashed`, `dotted`) | border width + colour only; borders are always solid |
| `text-shadow` | `font-effect: shadow(...)` |
| `outline` | `border` or a decorator |
| `object-fit` | the image decorator's fit modes (`fill`, `contain`, `cover`, `scale-none`, `scale-down`, `repeat`) |
| `position: sticky` | `fixed` or `absolute` |
| `::before` / `::after` / `content` | a real element |
| `aspect-ratio`, `writing-mode`, `list-style-*`, `font-variant`, `font-stretch` | no equivalent — restructure |
| `@import` | one `<link>` per stylesheet, or a single template link. See §10 |
| `@font-face` | faces are registered by the host: `uipreview --font`, plugin settings in engine |
| `calc()`, `clamp()`, `min()`, `max()` | precomputed values, or flexbox for proportional sizing |
| `:has()` | restructure, or set a class from C++ |
| `:disabled` on a generic element | a class such as `.is-disabled`. On a real form control it does work — see below |
| `start` / `end` / `normal` in flex properties | `flex-start` / `flex-end` / `stretch` |

**Value-level restrictions that are easy to miss:**

- `text-align: justify` parses without complaint and then does nothing — justification is not implemented, and the line falls back to left-aligned
- `line-height` has no `normal` — always give a number, length or percentage
- `font-family` accepts **one** family only, no fallback stack. A family name is one Folio registered, not a filename: the faces shipped with the plugin, anything the game drops in `ui/fonts/`, or a variable face registered under its own name with axes pinned
- `font-weight` has no relative weights (`bolder`, `lighter`); use `normal`, `bold`, or 1–1000. On a variable face any value in that range is real — it drives the `wght` axis directly rather than snapping to the nearest cut. Values are quantised in steps of 50, so `375` and `425` are distinct instances and `375` and `380` are not
- `font-style` has no `oblique`
- `border-radius` takes lengths only — no percentages, no two-axis (`/`) radii
- `position: fixed` behaves like `absolute` but ignores scrolling — it is not viewport-relative
- `cursor` takes an application-defined string, not CSS keywords
- **`rgba()`'s alpha channel is 0–255, not 0–1.** `rgba(0, 0, 0, 0.5)` parses to fully transparent. Write `#0000007f` instead — hex alpha is unambiguous, and `#rgb`, `#rgba`, `#rrggbb` and `#rrggbbaa` all work. `hsl()`, `hsla()`, `lab()`, `lch()`, `oklab()`, `oklch()` and the HTML colour names are also accepted
- `z-index` takes `auto` or a number — there is no `none`
- `clip` is not the CSS `clip` property. It takes `auto`, `none`, `always` or a number, and controls how many ancestor clip regions an element ignores
- `text-overflow` takes `clip`, `ellipsis`, **or a string** to use in place of the ellipsis
- `overflow` has no `clip` value: `visible`, `hidden`, `auto`, `scroll`
- `white-space`: `normal`, `pre`, `nowrap`, `pre-wrap`, `pre-line`. `word-break`: `normal`, `break-all`, `break-word`
- `vertical-align` adds `center` to the CSS set, and `top`/`bottom` align to the line box
- `float` is `none`/`left`/`right` only, and `drag` (`none`, `drag`, `drag-drop`, `block`, `clone`) is RmlUi's own
- `tab-index` is `none` or `auto` — it is a flag, not an ordering. Tab order is document order
- percentage `row-gap` **and** `column-gap` both resolve against the containing block's *height*

### Selectors

Supported structural selectors, in full: `:nth-child()`, `:nth-last-child()`, `:nth-of-type()`,
`:nth-last-of-type()`, `:first-child`, `:last-child`, `:first-of-type`, `:last-of-type`,
`:only-child`, `:only-of-type`, `:empty`, `:not()`, `:scope`.

Everything else written as `:something` is treated as a **pseudo-class** — a free-form flag the
host sets on an element. Unknown ones are not an error and are not warned about; they simply
never match. The pseudo-classes RmlUi actually sets are `:hover`, `:active`, `:focus`,
`:focus-visible`, `:checked`, `:selected` (a tab and its panel) and `:disabled`.

`:disabled` is the one to be careful with. It is set **only** by `input`, `select` and
`textarea`, and only in response to the `disabled` attribute, which also makes RmlUi write
`focus: none` onto the element as an inline property. Every other element — including a
`<button>`, which is not a real element type — never gets it, and `:disabled` there matches
nothing and warns about nothing. So: real form controls use the `disabled` attribute, and
everything else uses a class.

---

## 3. Layout

Flexbox is the primary tool. Absolute positioning is for anchoring a region, not for laying out
content within one.

**In a column flex container, `flex: 1 1 0` collapses the item.** A basis of `0` makes the
item's hypothetical main size zero, and in a container with no definite height that becomes the
container's height too — so a panel shrinks to its header and the body's content spills out
below it. Use `flex: 1 1 auto` for vertical fills. `flex: 1 1 0` is correct on the horizontal
axis, where it is how one cell absorbs the leftover width of a row.

**An `overflow: auto` container needs its scrollbar sized, or it destroys its own layout.**
RmlUi ships no scrollbar styling, and an auto width on a scrollbar resolves to the container's
full content width. The container lays out correctly right up until its content actually
overflows, at which point the scrollbar takes everything and every child collapses to its own
padding and border, with text inside anything `overflow: hidden` disappearing entirely.
`folio.rcss` sets the geometry so this cannot happen. Do not override `scrollbarvertical`'s
width with `auto`.

Use `dp` rather than `px` for anything that should scale with UI scale settings. Reserve `px`
for hairlines and values that must not scale.

The complete unit set: `px` `dp` `%` `em` `rem` `vw` `vh` `in` `cm` `mm` `pt` `pc`, `deg` and
`rad` for angles, `x` for resolution, and unitless numbers. There is no `ch`, `ex`, `fr`, `vmin`
or `vmax`. A number written without a unit where a length is expected is a parse warning, not an
implied `px` — the one exception is `line-height`, which is unitless by design.

---

## 4. Animation

**Animate `transform`, `opacity`, `color`, `background-color`, `border-color` and `filter`.**
These do not trigger reflow.

**Never animate `width`, `height`, `margin`, `padding`, `top`/`right`/`bottom`/`left`,
`font-size` or `line-height`.** Each triggers layout, and a HUD animating these every frame will
be measurably slower than the UMG implementation it replaced.

To grow or shrink something, animate `transform: scaleX()` on an element with a fixed box. To
slide something, animate `transform: translate()`, not `left`.

`transition` fires only when a class or pseudo-class changes. For continuous or self-driving
motion, use `@keyframes` with `animation`.

Do not use 3D transforms (`rotateX`, `rotateY`, `perspective`). They do not map onto the
renderer. 2D only: `translate`, `scale`, `rotate`, `skew`.

**Times are seconds, and `ms` does not exist.** `transition: opacity 120ms` does not warn and
does not animate: the parser reads `120` as a *reverse adjustment factor* and the transition
ends up with no duration. Always write `0.12s`.

**The easing names are RmlUi's, not CSS's.** There is no `ease`, `ease-in`, `ease-out`,
`ease-in-out`, `linear`, `step-*` or `cubic-bezier()`. The set is `<family>-in`, `<family>-out`
or `<family>-in-out` for each of `back`, `bounce`, `circular`, `cubic`, `elastic`,
`exponential`, `linear`, `quadratic`, `quartic`, `quintic`, `sine`. A misspelled easing name is
read as a property name instead, which silently produces a transition that never fires.

```css
.btn { transition: background-color 0.12s cubic-out, transform 0.12s cubic-out; }
```

`transition: all 0.12s` is legal but transitions every animatable property including layout
ones. Name the properties.

---

## 5. Decorators, filters and effects

### The complete effect vocabulary

Anything not on these lists does not exist. A misspelled name is a warning, never an error, and
produces no output.

**`decorator` and `mask-image`** take: `image`, `ninepatch`, `tiled-horizontal`,
`tiled-vertical`, `tiled-box`, `text`, `shader`, `gradient`, `horizontal-gradient`,
`vertical-gradient`, `linear-gradient`, `repeating-linear-gradient`, `radial-gradient`,
`repeating-radial-gradient`, `conic-gradient`, `repeating-conic-gradient`. `@decorator` names a
reusable one.

**Multiple decorators paint in reverse source order** - the first named is on top, exactly like
CSS `background-image` layers. Written the intuitive way round, an effect listed after an opaque
background is painted underneath it and appears to do nothing.

**`filter` and `backdrop-filter`** take: `blur`, `drop-shadow`, `brightness`, `contrast`,
`grayscale`, `invert`, `opacity`, `saturate`, `sepia`, `hue-rotate`.

**`backdrop-filter` additionally takes `outside-blur`, which is Folio's own.** Where `blur`
softens what is behind an element, `outside-blur` softens everything the element is not - the
modal scrim, where the screen goes soft and the dialog stays sharp:

```css
.dialog { backdrop-filter: outside-blur(8dp); }
```

Two limits. The hole is a rectangle, so a `border-radius` dialog still cuts a square one. And it
costs four blurred regions rather than one, because a rectangle cannot be cut out of another
rectangle.

**`font-effect`** takes: `glow`, `outline`, `shadow`, `blur`, and Folio's own `fill-image`.
These are the only way to get a text shadow; `text-shadow` does not exist.

### The shader decorator

`decorator: shader("...")` reaches the effects RCSS has no syntax for, additive blending first
among them:

```css
.hit    { decorator: shader("additive #ff5028c0"), image(panel); }
.crt    { decorator: shader("scanlines #7fe8ff90 0.16 0.5"), image(panel); }
.sheen  { decorator: shader("sweep #ffffffb0 0.35 0.3 15"), image(panel); }
```

- `additive <colour>` - a flat wash that adds instead of covering. Hit flashes, charge glows.
- `scanlines <colour> <period> <thickness>` - period and thickness as fractions of the box.
- `sweep <colour> <position> <width> <angle>` - a soft band across the box, angle in degrees.

`additive` and `sweep` add rather than cover. There is no blend state involved: RmlUi composites
premultiplied, so a colour written with zero alpha adds to what is under it, and that works
identically in both renderers.

Each is evaluated into a small texture rather than run as a fragment shader, because Slate has no
seam for one through `MakeCustomVerts` and none of these needs it. The consequence is that the
set is fixed - a name Folio does not know warns once and draws nothing.

### Renderer support

Every one of these depends on a render-interface feature — layers, clip masks, filter and shader
compilation. `uipreview` implements the complete set. **The Unreal renderer does not yet**, so
some preview correctly and render as nothing in engine:

| | In engine |
|---|---|
| Every `*-gradient` decorator, `image`, `ninepatch`, `tiled-*` | Works |
| `border-radius`, borders, `transform`, `opacity` on an element | Works |
| `box-shadow`, clip masks, render layers, `filter: blur` | Works. Layers are render targets and clip masks are a real stencil, so an inverse mask - which is what stops an outer shadow painting inside its own element - is exact |
| `backdrop-filter: blur` | Works, and is the one effect that reaches outside the UI: it blurs the backbuffer, so the 3D scene behind a HUD panel blurs with it. The blurred region is a rectangle, so a `border-radius` corner blurs square |
| `backdrop-filter: outside-blur` | Works. Folio's own; see below |
| Text, at any size or transform scale | Works. See §6 |
| `font-effect`, all four | Works, in both renderers, resolved from the field. Bounded in reach - see §6 |
| **`filter` other than `blur`, `mask-image`, `backdrop-filter` other than `blur`** | **Banned.** The layer machinery is in place and only the shaders are missing, so these parse, log once and render nothing |

A surface drawn through a layer lands about 3/255 darker than the same colour drawn directly,
from the 8-bit sRGB round trip through the render target. Visible only side by side, and not
worth a wider target format.

`decorator: shader(...)` reaches additive blending, scanlines and sweeps. See §6.

### `@media` features

`width`, `min-width`, `max-width`, `height`, `min-height`, `max-height`, `aspect-ratio`,
`min-aspect-ratio`, `max-aspect-ratio`, `resolution`, `min-resolution`, `max-resolution`,
`orientation` (`landscape`/`portrait`), `theme`.

**Resolution is `resolution`, not `dp-ratio`**, and its unit is `x`:

```css
@media (min-resolution: 2x) { ... }
```

Features chain with `and`, and a single leading `not` may negate the whole query:

```css
@media not (orientation: portrait) and (min-width: 1280px) { ... }
```

There are no media *types* (`screen`, `print`) and no comma-separated query lists — a comma is a
parse warning, not an alternative.

---

## 6. Text

Folio replaces RmlUi's font engine outright. Glyphs are **MTSDF** — three channels of
multi-channel distance field, whose median reconstructs sharp corners, plus a true signed
distance in alpha that the effects read.

**Type scales freely.** Glyphs are generated once per face into a size-independent atlas, so a
`font-size` of 13dp and one of 96dp cost the same atlas and both stay sharp — as does the same
text under `transform: scale`, at 4K, or at any DPI. Pick sizes for the design, not for what an
atlas was baked at.

The one thing to watch is the opposite of the old worry: distance-field text has no hinting and
no stem snapping, so at 12–14dp on a 1080p display stems are slightly less uniform than a hinted
rasteriser would give. Check small body text on the target display before committing to it.

**Advances are unhinted and scaled linearly from design units.** That is what lets one atlas
serve every size, and it means text wraps in slightly different places than a hinted rasteriser
would. It also means line breaks do not survive a change of dp ratio: an advance is a whole
number of pixels at each size, so doubling the ratio halves the relative rounding error and
words move. Never size a container to fit its content exactly at one ratio.

### Variable fonts

`font-weight: 1..1000` drives the `wght` axis directly, quantised in steps of 50. `opsz`, where
a face has it, is driven automatically from the requested size. Other axes — `wdth` and
anything custom — are pinned at registration in C++ and reached by family name:

```cpp
Folio::RegisterVariableFace("Inter Condensed", "<path>", {{"wdth", 75}});
```

```css
.narrow { font-family: Inter Condensed; }
```

### Font effects

All four are resolved from the field's true-distance channel rather than baked into extra glyph
textures, so each costs one more quad per glyph and no atlas at all.

**Their reach is bounded, and the bound is in em, not pixels** — currently 0.104em at the
default atlas settings. `outline(2px)` is a twentieth of an em on 40dp text and well inside it;
on 13dp text it is a sixth of an em and gets clamped, with a warning naming the limit. Size an
effect against the text it decorates, and check small text specifically.

The bound is a little under half the field's range rather than exactly half. Past half, the
field is saturated: a texel an em away from the glyph holds the same number as one just beyond
the edge, so an effect whose edge lands there has no outside to fade into. The last texel is
held back to give it one.

Clamping is a real limit, not a soft one. An effect asked for at three times the cap looks
identical to one asked for at the cap. To go wider, raise `pxrange` when baking, at a quadratic
cost in atlas area.

### Textured text

`fill-image` fills the glyphs with a pattern instead of a flat colour - a scanline, a gradient, a
gold-leaf sheet:

```css
.crt { font-effect: fill-image("/ui/art/scanlines.png" 4dp); }
```

The arguments are a path, a tile size, and an optional colour the pattern is multiplied by.
Three things to know:

- **It names a file, not a sprite.** A font effect is built while the stylesheet is parsed, and
  has no route to the spritesheet a decorator would use.
- **The pattern is anchored to the screen, not to the text.** A scanline stays put while text
  scrolls under it, which is what a scanline is for and wrong for anything meant to travel with
  the words.
- **It only textures the fill.** It is a front layer over the glyph, so an `outline`, `glow` or
  `shadow` on the same text stays flat. Where the pattern is transparent the flat `color` shows
  through, so the two compose.

Not available inside a layer - text under a `filter`, a `mask-image`, or an `opacity` on an
ancestor. Those paths bind one texture, and the pattern is skipped.

---

## 7. Vector sprites

An icon can come from an SVG instead of a PNG, baked to the same kind of distance field the text
uses. `Tools/msdfbake vector` reads the SVG, packs the fields into an atlas and writes a
`@spritesheet` block. Markup does not change: `<img sprite="icon_sword">` and
`decorator: image(icon_sword)` work exactly as they do for raster sprites.

Use it for art that has to survive extreme magnification — a HUD icon that also appears on a
full-screen map, anything on a world-space surface the player can walk up to. Everything else
stays raster; the vector path costs a draw per colour.

The rules, all of which come from what a distance field can represent:

- **A field is coverage and nothing else.** One field is one silhouette. Multi-colour art is
  baked as a **stack of silhouettes**, one per fill colour, painted back to front - still one
  atlas, still resolution independent, at the cost of one draw per layer. Keep the colour count
  low deliberately.
- **A stack needs its frame image.** Every layer is absolutely positioned and so adds nothing to
  its container's height; on its own the container collapses and nothing renders. The baker emits
  a `<name>_frame` sprite carrying the drawing's proportions, and a hidden `<img>` on it is what
  gives the box a height:

  ```html
  <div class="knight-stack" style="width: 240dp">
    <img class="knight-frame" sprite="knight_frame"/>
    <img class="knight_0-ink" sprite="knight_0"/>
    <img class="knight_1-ink" sprite="knight_1"/>
  </div>
  ```

  Set the width and let the frame find the height. RCSS has no `aspect-ratio`, and an image
  element's intrinsic size is the only thing that carries one.
- **`ninepatch` and `tiled-box` are invalid on a vector atlas.** Non-uniform stretching corrupts
  the distance metric, so corners round off and edges soften. A vector sprite scales uniformly
  or not at all.
- **Set width or height, not both.** They resolve against different bases - the parent's width
  and the parent's height - so a stack sized as a percentage of both squashes whenever the window
  aspect changes. One dimension preserves the intrinsic ratio.
- The vector sheet is declared `1x`. There is no second resolution to author.
- **`clip-path` is honoured.** nanosvg discards it, so the baker reads the relationship out of the
  SVG itself and intersects the geometry with Skia's path operations - the same library that
  unions a glyph's overlapping contours. A clip path's own shape is a stencil and is never drawn,
  which is what SVG says and what nanosvg gets wrong.
- **Strokes are outlined into area**, since a field has no notion of a line. Joins (miter with
  its limit, round, bevel) and caps (butt, round, square) are read from the SVG and honoured. A
  stroke costs its own layer, because it is its own colour.

**Baking is not automatic.** Re-run `msdfbake vector` after changing an SVG.

RmlUi caches a texture by its path and never looks at the file again, so a re-baked atlas would
otherwise keep rendering as it was. Reopening a document picks up anything whose timestamp has
moved; `folio.ReloadTextures` does it without reopening, and `folio.ReloadTextures all` releases
every texture whether or not it looks changed. `uipreview` handles it through its file watcher
and needs nothing.

---

## 8. Navigation

Navigation is RmlUi's built-in spatial navigation. **Do not implement custom focus logic.**

**`nav` is read from the focused element itself, and it does not inherit.** This is the single
most important thing on this page. When a direction key arrives, RmlUi reads `nav-<direction>`
as a *local* property of the element that currently has focus. A container's `nav` is never
consulted, and `nav` set on an ancestor is never inherited down. Verified directly: three
`tab-index: auto` items inside a `.list { nav: vertical; }` container navigate exactly once,
into the first item, and then stop dead.

```css
/* Does nothing. */
.menu { nav: vertical; }

/* Correct: the property goes on the focusable items. */
.menu-item { tab-index: auto; nav: vertical; }
```

So: **every focusable element carries its own `nav`**, declared next to its `tab-index: auto` in
whatever rule defines the component.

The rest:

- `nav: auto` on `body`. The document is the focus holder before anything else is focused and after focus is lost, and this is what lets the first direction press enter the content. Without it a fresh document is unreachable by controller. It is also the recovery path after a mouse click lands on nothing
- Values are `none`, `auto`, `horizontal`, `vertical`, or an `#id` string. `horizontal` and `vertical` are filters on the *incoming direction*, not on where the search looks: `nav-up: vertical` passes (up is a vertical direction), `nav-up: horizontal` blocks. So the useful form is the single value — `nav: vertical` on a menu item means up and down navigate and left and right do nothing at all
- `nav` is a **box shorthand** in `up right down left` order, exactly like `margin`. Which makes `nav: vertical horizontal` a trap: it reads as up/down `vertical` and left/right `horizontal`, every one of which passes its own filter, so it is an elaborate way of writing `nav: auto`
- Axis locking keeps focus from jumping sideways into unrelated chrome, but it also stops focus *leaving*. A dialog's action row wants `nav: auto` so up returns to the content; a long settings list wants `nav: vertical` so a stray left press does not land in the sidebar. Choose per component, not globally
- `nav-down: #element_id` to override when the spatial heuristic picks wrong. A missing id warns at load
- **Both ends must agree.** Navigation is resolved entirely from the source element, so `a { nav-down: #b }` does not give you `b { nav-up: #a }`. Explicit overrides come in pairs
- `tab-index: auto` makes an element focusable, and nothing else does. It is a flag, not an order — Tab walks document order
- `focus: none` makes an element and its whole subtree unfocusable, and it inherits. It also suppresses `click`, because the click is dispatched to the nearest *focusable* ancestor rather than to what was under the cursor — so a `focus: none` element silently forwards its clicks to whatever contains it. `mousedown`, `mouseup`, `mouseover` and `:hover` still fire on it. `pointer-events: none` is what takes it out of hit testing entirely. Disabled controls want all three: a class for the look, `focus: none` to leave navigation, `pointer-events: none` to stop responding
- Enter, numpad Enter and Space on a focused element with `tab-index: auto` synthesise a click. You get keyboard and gamepad activation for free, and you should not add a separate keydown path for it
- The spatial search starts from the focused element's **nearest scroll container**, and any element whose `overflow` is not `visible` on either axis is a scroll container. An incidental `overflow: hidden` on a layout wrapper will therefore confine focus somewhere you did not intend, and it reads as a navigation bug rather than a styling one. If focus refuses to leave a region, look for `overflow` first
- **A modal is only half-trapped by that rule.** `overflow: hidden` keeps navigation inside the dialog once focus is already there, but nothing puts it there. A fresh document has nothing focused, and the first direction press runs the *document's* `nav`, which takes the first tabbable element in document order — the content behind the dialog. Make the background inert (`focus: none`, which inherits down the whole subtree, plus `pointer-events: none`) for as long as the dialog is up. Verified: without it a scripted `down,right,up,left` walks the buttons behind the dialog and never reaches it

Style the focus state with `:focus-visible`, never `:focus`. `:focus-visible` is set by
navigation actions specifically, so the focus ring correctly stays hidden during mouse use.

### What counts as a direction

Everything below arrives as the same four keys, and all of it is live at once. A document never
learns what produced a direction.

| | |
|---|---|
| Arrow keys, **WASD**, **IJKL** | up, down, left, right |
| D-pad, and either stick pushed to its extreme | the same four |
| Either analog stick, held | repeats: 0.5 deadzone, 0.4s before the first repeat, 0.12s after |
| `Enter`, `Space`, gamepad **confirm** face button | activate the focused element |
| `Escape`, the other face button | back |
| `Tab`, either shoulder | tab order |

**Letters only navigate when nothing is taking text.** With an `input` or `textarea` focused, W is
a W. That is what lets three keyboard schemes coexist without any of them breaking typing.

**Each scheme can be switched off** by a project, from `DefaultEngine.ini` or the console, with no
rebuild: `folio.Nav.Arrows`, `folio.Nav.WASD`, `folio.Nav.IJKL`, `folio.Nav.Gamepad`. A game that
binds WASD to movement while a HUD is up wants that one off. Turning the arrows off still leaves
them working inside a text field, where they mean something else.

**UI sounds** are reported, not played: `FFolioModule::OnUiSound()` fires `Focus`, `Confirm`,
`Back` or `Invalid`. Folio owns no sound assets. `Invalid` is a click on something carrying
`.is-disabled`, which is the case worth a different noise.

### Which face button confirms

South (A, Cross) confirms by default and east (B, Circle) goes back. Nintendo hardware and
Japanese conventions are the other way round, so it is a setting, not a constant:

```cpp
FFolioModule::Get().SetConfirmButton(Folio::EConfirmButton::East);
```

or `folio.Nav.SwapConfirmBack 1` from the console or a per-platform `DefaultEngine.ini`. Changing
it swaps what the two buttons *do* and updates every open document in the same frame. **Folio has
no platform opinion of its own** — it does not check what it is running on, because the answer
depends on region as much as hardware. A port decides and calls the setter.

### Telling a document what the player is holding

Folio tracks the last device used — keyboard, mouse or gamepad — process-wide rather than per
document, so a HUD and a pause menu open together cannot disagree. It reaches markup two ways,
and most UI only needs the first.

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

`folio-input-*` is redundant with `folio-device-*` on purpose: keyboard and mouse are both "not a
gamepad", and without the coarse pair every rule needs a duplicate selector or `:not()`.

**The reserved `folio` data model**, for the rest — when an expression has to *build* a value:

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

`folio` is **a reserved model name — do not use it for game data.** It is mounted on every context
whether a document binds it or not, so a game that never touches it pays one unused model. An
element binds one model at a time, so a screen with its own model reads these from a nested
subtree, as above, rather than from `<body>`.

Both routes are pinned in the preview with `--input-device keyboard|mouse|gamepad` and
`--confirm-button south|east`, which is how a gamepad screenshot is taken without a gamepad.

From C++, `FFolioModule::Get().GetInputDevice()` and `OnInputDeviceChanged()`.

---

## 9. Data binding

There is **no JavaScript**. Interactivity is expressed three ways: data bindings for anything
driven by game state, state classes and pseudo-classes for appearance, and named events with C++
listeners for actions.

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

`{{ }}` interpolation is text-only and only inside an element's text content — not in attribute
values, where `data-attr-*` is the mechanism.

**`{{ items.size }}` gives an array's length**, and works anywhere an expression does -
`data-if="items.size == 0"` for an empty-list message, `{{ items.size }} items` for a count.

This is Folio's, not RmlUi's. Upstream calls `DataVariable::Size()` from `data-for` and nowhere
else, so nothing in an expression can reach a length; a transform function cannot help either,
because transforms take Variants and an array variable cannot produce one. Both data models
answer a `size` member on an array instead. An array has no named members of its own, so nothing
is shadowed.

Inside a `data-for`, `it_index` is the zero-based index and is always available.

---

## 10. Stylesheet composition

**RCSS has no `@import`.** A stylesheet cannot pull in another one. There are exactly two ways
to get rules into a document:

```html
<link type="text/rcss" href="/ui/lib/button.rcss"/>   <!-- one per stylesheet -->
<style> /* document-local rules */ </style>
```

Listing eight component stylesheets in every document is how this becomes unmaintainable, so use
the one aggregation mechanism RmlUi does have: **a template's head is merged into any document
that links it.**

```html
<link type="text/template" href="/ui/lib/lib.rml"/>
```

A template whose `<head>` links every component stylesheet gives a document the whole library in
one line. The document does not need to *use* the template — merely linking it merges the head.
Verified directly.

Order: template heads merge first, then the document's own head, so a document's `<style>` block
always wins over the library. Within the library, later `<link>`s win at equal specificity.

---

## 11. Element vocabulary

RML is not HTML. Only these tags are backed by real element types with behaviour:

| Tag | What it does |
|---|---|
| `body` | The document element |
| `img` | Image. Takes `sprite="name"`, or `src` + `rect` |
| `handle` | Drag/resize handle, used with the `drag` property |
| `form`, `input`, `select`, `textarea`, `label` | Form controls. `input` types: `text`, `password`, `checkbox`, `radio`, `range`, `submit` |
| `progress` / `progressbar` | Takes `value`, `max`, `direction` (`top`, `right`, `bottom`, `left`, `clockwise`, `counter-clockwise`) and `start-edge`, plus the `fill-image` property |
| `tabset` | `<tabset><tabs><tab>…</tab></tabs><panels><panel>…</panel></panels></tabset>`. Sets `:selected` on the active tab and its panel, and `display: none` on the others, as inline properties |

**A tabset silently grows a second, empty `tabs` and `panels` pair.** It creates either child on
demand whenever it looks for one and does not find it, and during parsing it looks before the
markup's own children are in place — so a tabset that declares both still ends up with four
children: the real pair, then an empty pair appended after them. Everything still *works*,
because it always takes the first match. But a bare `tabs` or `panels` tag selector styles two
elements, and any border or background on it is drawn a second time at the bottom of the tabset.
It looks like a layout bug in the panel. Always write `tabs:first-of-type` and
`panels:first-of-type`.

**Every other tag is a generic element.** `div`, `p`, `h1`, `span`, `button`, `ul`, `li`,
`section`, `nav` and anything else you invent all instance the same class and differ only in
what the stylesheet says about them. That is a feature: a `<menuitem>` tag is as real as a
`<div>`, and reads better. It also means `<button>` is not a button — it has no focus behaviour
and no click handling until it gets `tab-index: auto`.

Tag names are matched literally in selectors, so keep the invented ones documented.

---

## 12. Localisation

Folio's mechanism, which a game's own conventions will have policy on top of.

**A user-visible string carries a `data-loc` key:**

```html
<p data-loc="start">Start Game</p>
```

The source text stays inline so it is readable and previewable. The key makes it gatherable into
Unreal's localisation pipeline.

**A `data-loc` element holds text and nothing else.** Translation replaces the element's entire
contents, so a child element inside it is swallowed into the string and comes back out as
escaped, visible markup — a tab reading `Accessibility<span class="tab-badge" />`. Put the key
on an inner text-only element and keep the decoration as its sibling. `uipreview` reports this
as `W-LOC-MARKUP`.

```html
<tab class="tab focusable"><span data-loc="accessibility">Accessibility</span><span class="tab-badge"/></tab>
```

`data-noloc` is the explicit opt-out for strings that are not language: numerals, timers, player
names, version strings. It is greppable, so what has been excluded stays reviewable. Text
produced by a data binding needs neither — it was already `FText` in C++ and is gathered by the
normal source pass.

`uipreview --culture pseudo --strict` reports untagged literal text as `W-LOC-UNTAGGED`. Note
that **`--strict` alone does not run the localisation lint** — the check only runs when a culture
is loaded, so the two flags go together.

Assume translated strings are up to 40% longer than English.

---

## 13. Verifying a document

```
uipreview <file.rml> --size 1920x1080
uipreview <file.rml> --culture pseudo --strict --screenshot pseudo.png
uipreview <file.rml> --keys "down,down,right,enter" --shot-each out/
uipreview <file.rml> --json-report report.json
```

`--strict` is the one that matters, because nearly every RCSS mistake is a warning rather than
an error in RmlUi and a check that only looks at errors will pass a thoroughly broken
stylesheet. A non-zero exit code means the document is invalid.

### The harness

`scripts/verify.ps1` runs every document in `Tools/uipreview/tests/` under `--strict` at two dp
ratios, writes a metrics report for each, optionally diffs them against a previous run, and
takes the screenshots that have to be judged by eye.

```powershell
.\scripts\verify.ps1 -Out build\verify-before
# ...change something...
.\scripts\verify.ps1 -Baseline build\verify-before
```

`--metrics-report` is what makes a text-engine change reviewable. It records where every text
element and every wrapped line landed, and the diff reports lines that broke somewhere new —
the regression this system is most likely to cause and the least likely to be noticed by eye.
Compare like for like: same size, same dp ratio.

`scripts/check_shader_parity.ps1` fails if `Shaders/Private/FolioMsdf.ush` and the GLSL in
`Tools/uipreview/src/MsdfProgram.cpp` have drifted. `build.ps1` runs it before compiling. A
golden-image comparison cannot catch this — the two renderers' colour pipelines never match
pixel for pixel, so only the source can be compared.

`build.ps1 -Asan` for any intermittent crash. Lifetime bugs here tend to read freed memory that
is still intact, so they survive for weeks and then fail somewhere else.

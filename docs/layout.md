# Layout and panels

Flexbox, and almost nothing else. RCSS has no grid, no `calc()`, no `aspect-ratio` and no custom
properties, so the layout vocabulary is small and you will use all of it.

## Copy a screen before you lay one out

`rcss/recipes/` holds complete working screens — `main_menu.rml`, `pause.rml`, `confirm.rml`,
`nameplate.rml`. **Copy one into your own `ui/` and edit it.** They pass `--strict` and `--a11y` with
no findings, so what you start from already has the `box-sizing`, flex bases, scrollbar geometry,
`tab-index` and `nav` right — and every one of those is a trap documented further down this page.

Copied, not linked: a recipe is a starting point you own, and nothing in one names Folio's directory
except the library link. See **[components, themes and recipes](components_and_themes.md)**.

The rest of this page is for when you need something no recipe covers.

## Start from the defaults

RmlUi ships no user-agent stylesheet. Every element starts from raw initial values, and several are
not what a browser would give you:

| | RCSS starts at | So |
|---|---|---|
| `display` | `inline` | Including `div` and `p`. Folio's `folio.rcss` fixes this; without it a document is one run of overlapping text. |
| `color` | white | Not black. |
| `font-size` | `12px` | Small, and in `px` - it does not scale with UI scale until you set it. |
| `font-family` | *(empty)* | **No text renders at all** until a family is named. |
| `line-height` | `1.2` | A unitless multiplier. There is no `normal`. |
| `box-sizing` | `content-box` | Padding and border add to `width`. Most components want `border-box`. |
| `tab-index` | `none` | Nothing is focusable until you say so. |
| `nav-*` | `none` | Directional input does nothing until you say so. |

The document `body` is laid out shrink-to-fit against the context regardless of `display`.
`folio.rcss` sets `width: 100%`; add `height: 100%` yourself if you want a full-viewport background.

## Units

`px` `dp` `%` `em` `rem` `vw` `vh` `in` `cm` `mm` `pt` `pc`, `deg` and `rad`, `x` for resolution, and
unitless numbers. There is no `ch`, `ex`, `fr`, `vmin` or `vmax`.

**Use `dp` for anything that should scale with the player's UI scale setting.** Reserve `px` for
hairlines and for values that must not scale.

A bare number where a length is expected is a **parse warning, not an implied `px`**. The one
exception is `line-height`, which is unitless by design.

## Flexbox

Fully supported and the primary tool. `flex` `flex-basis` `flex-direction` `flex-flow` `flex-grow`
`flex-shrink` `flex-wrap`, `align-content` `align-items` `align-self`, `justify-content`, `gap`
`row-gap` `column-gap`.

Two value-level differences from CSS: use `flex-start` / `flex-end` / `stretch`, not `start` / `end` /
`normal`. And percentage `row-gap` **and** `column-gap` both resolve against the containing block's
*height*, which is a genuine RmlUi quirk rather than a typo here.

### The one that catches everyone

**In a column flex container, `flex: 1 1 0` collapses the item.**

A basis of `0` makes the item's hypothetical main size zero, and in a container with no definite
height that becomes the container's height too. A panel shrinks to its header and the body's content
spills out below it.

```css
/* Wrong on a column. The body collapses and the panel is header-height. */
.panel-body { flex: 1 1 0; }

/* Right. */
.panel-body { flex: 1 1 auto; }
```

`flex: 1 1 0` is correct on the **horizontal** axis, where it is how one cell absorbs the leftover
width of a row. The rule of thumb: `auto` for vertical fills, `0` for horizontal shares.

## Scrolling

`overflow` takes `visible`, `hidden`, `auto`, `scroll`. There is no `clip`.

**An `overflow: auto` container needs its scrollbar sized, or it destroys its own layout.** RmlUi
ships no scrollbar styling, and an `auto` width on a scrollbar resolves to the container's full
content width. The container lays out correctly right up until its content actually overflows, at
which point the scrollbar takes everything, every child collapses to its own padding and border, and
text inside anything `overflow: hidden` disappears entirely.

`folio.rcss` sets the geometry so this cannot happen. **Do not override `scrollbarvertical`'s width
with `auto`.**

There is a second consequence, and it reads as a navigation bug rather than a layout one: **any
element whose `overflow` is not `visible` on either axis is a scroll container**, and the spatial
focus search starts from the focused element's nearest one. An incidental `overflow: hidden` on a
layout wrapper will confine focus somewhere you did not intend. If focus refuses to leave a region,
look for `overflow` first. See **[input and navigation](input_and_navigation.md)**.

## Positioning

`static`, `relative`, `absolute`, `fixed`. **`fixed` behaves like `absolute` but ignores scrolling** -
it is not viewport-relative. There is no `sticky`.

Absolute positioning is for anchoring a region, not for laying out content within one. If you find
yourself positioning six things absolutely inside a panel, the panel wants to be a flex container.

`z-index` takes `auto` or a number. There is no `none`.

## Sizing to content

There is no `aspect-ratio`. An image element's intrinsic size is the only thing in RCSS that carries
a ratio, which is why the vector layer stack uses a hidden `<img>` to give its container a height -
see **[vector art](vectors.md)**.

**Set width or height on a ratio-preserving box, not both.** They resolve against different bases -
the parent's width and the parent's height - so a box sized as a percentage of both squashes whenever
the window aspect changes.

**Never size a container to fit its text exactly.** Advances are unhinted and scaled linearly, so a
line breaks in slightly different places at different dp ratios. A container that fits at ratio 1
will clip at ratio 2. Leave slack, or let the container grow.

## Tables

`display: table`, `table-row-group`, `table-row`, `table-column-group`, `table-column`, `table-cell`
all exist and work. Use them for actual tabular data - a keybinding list, a stats readout - where
flexbox would need a fixed width on every cell to line the columns up.

## What does not exist

The full list is in **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §2. The layout ones worth
memorising:

| Instead of | Use |
|---|---|
| `display: grid`, every `grid-*` | flexbox, or `display: table` for real tables |
| `calc()`, `clamp()`, `min()`, `max()` | precomputed values, or flexbox for proportional sizing |
| `aspect-ratio` | an image's intrinsic size, or fixed dimensions |
| `position: sticky` | `fixed` or `absolute` |
| `::before` / `::after` / `content` | a real element |
| `var()` and `--custom-properties` | concrete classes in a token stylesheet - see **[themes](components_and_themes.md)** |

None of these warn. They parse and do nothing.

## The shipped panels

`rcss/lib/` has the structure for panel, list, modal, tabbar and the rest, with no colour in any of
them. Compose those rather than starting from a bare `div` - they already have the `box-sizing`, the
scrollbar geometry and the flex bases right.

`rcss/recipes/` assembles them into whole screens, which is the level to start from if one of them is
close to what you want. See **[components, themes and recipes](components_and_themes.md)**.

## Where to go next

- **[Components, themes and recipes](components_and_themes.md)** — the library to build out of.
- **[Testing a document](testing_a_document.md)** — `--strict`, and checking two dp ratios.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §1 and §3 — the complete property set.

# Building UI with Folio

You have been asked to build UI for a project that has this plugin. Read this page before writing
anything. It is short, and it is the difference between working UI and UI that renders nothing while
reporting no errors.

## The one thing that will catch you

**RmlUi is not a browser and RCSS is not CSS.** It is close enough that valid-looking CSS parses
cleanly and then does nothing at all, with no warning:

`display: grid` · `var()` and `--custom-properties` · `background-image` · `text-shadow` · `outline` ·
`calc()` · `::before` / `::after` / `content` · `aspect-ratio` · `position: sticky` · `@import` ·
`@font-face` · `object-fit` · `border-style` · `:has()`

And these are wrong in ways that look right:

| Written | What happens |
|---|---|
| `transition: opacity 120ms` | **No animation.** Times are seconds; `ms` does not exist. Write `0.12s`. |
| `transition: opacity 0.12s ease-out` | **No animation.** Easings are RmlUi's. Write `cubic-out`. |
| `rgba(0, 0, 0, 0.5)` | **Fully transparent.** Alpha is 0-255. Write `#0000007f`. |
| `line-height: normal` | No such value. Give a number. |
| `font-family: Inter, sans-serif` | One family only. No stacks. |
| `.menu { nav: vertical; }` | **Does nothing.** `nav` is read from the focused element and does not inherit. |
| `flex: 1 1 0` in a column | **Collapses the item.** Use `flex: 1 1 auto` vertically. |

**[UI_CONVENTIONS.md](UI_CONVENTIONS.md) is authoritative for capability.** When something does
nothing, look there before debugging. It is usually on the banned list.

## Do this, in this order

### 1. Check what the project already has

```bash
ls ui/                      # screens, lib, art, fonts, mock
cat ui/theme.rcss           # the game's palette, if it has one
ls Plugins/Folio/rcss/recipes/
```

If the project has its own conventions document, it governs taste and this one governs what exists.

### 2. Start from a recipe, not from nothing

`Plugins/Folio/rcss/recipes/` has complete working screens - `main_menu.rml`, `pause.rml`,
`confirm.rml`, `nameplate.rml`. **Copy one into `ui/screens/` and edit it.** They pass `--strict` and
`--a11y` with no findings, so what you start from is already clean.

### 3. Link the library in one line

RCSS has no `@import`. A template's head merges into any document that links it, which is the only
aggregation mechanism there is:

```html
<link type="text/template" href="/folio/lib/lib.rml"/>
```

That gives you the whole component library. Then compose its classes rather than styling bare
`div`s - the components already have the `box-sizing`, flex bases, scrollbar geometry, `tab-index`
and `nav` right, and every one of those is a documented trap.

### 4. Put colour in the right place

| | |
|---|---|
| `/folio/lib/*.rcss` | structure only, **no colour anywhere** |
| `/folio/themes/default.rcss` | the tokens and the skin |
| the game's own stylesheet | anything it disagrees with |

RCSS has no `var()`, so a theme is an override block and the later rule wins. **Never put a colour in
a component rule** - it can then only be overridden by restating the rule in full.

Use the token classes (`.c-accent`, `.t-title`, `.s-pad-m`, `.focusable`) and retune what they mean,
rather than inventing parallel ones.

### 5. Make it navigable

**Every focusable element carries its own `tab-index: auto` and its own `nav`.** A container's `nav`
is never consulted.

```css
.menu-item { tab-index: auto; nav: vertical; }
body       { nav: auto; }            /* or the first direction press goes nowhere */
```

`<button>` is not a button - only `body`, `img`, `handle`, the form controls, `progress` and `tabset`
are real element types. Everything else is a generic element with no behaviour until you give it
`tab-index: auto`. With that, Enter, Space and the gamepad confirm all synthesise a click for free.

Style `:focus-visible`, never `:focus`.

### 6. Label anything a player reads

```html
<p data-loc="start">Start Game</p>
<button class="btn focusable" id="icon" folio-label="Settings"><img sprite="icon_gear"/></button>
```

`data-loc` on every user-visible string, `data-noloc` on the ones that are not language, and
`folio-label` on any control whose visible content is not its name. Both are cheap now and expensive
to retrofit across a finished game.

### 7. Verify before you say it is done

```bash
./Plugins/Folio/bin/win64/uipreview.exe ui/screens/yours.rml --root . --strict --a11y
```

**Non-zero exit means it is not right.** Nearly every RCSS mistake is a warning rather than an error,
so a check that only looks at errors passes a thoroughly broken stylesheet.

Then take a look at it:

```bash
./Plugins/Folio/bin/win64/uipreview.exe ui/screens/yours.rml --root . --screenshot out.png
```

Actually look at the screenshot. It is the only way to catch a layout that is valid and wrong.

## Quick reference

**Layout** is flexbox. No grid, no `calc()`, no `aspect-ratio`. `dp` for anything that should scale
with the player's UI scale; `px` only for hairlines.

**Colour** is hex with hex alpha: `#1f1f28`, `#0000007f`.

**Animate** `transform`, `opacity`, `color`, `background-color`, `border-color`, `filter`. **Never**
`width`, `height`, `margin`, `padding`, `top`/`left`, `font-size` - each triggers layout every frame.

**Images** are sprites from a `@spritesheet`, drawn with `<img sprite="...">` or
`decorator: image(...)`. Tint single-colour art with `image-color` rather than shipping four
variants.

**Text** is distance-field and scales freely - pick sizes for the design. Font effects reach about
0.104em, so an outline on small text gets clamped.

**Drag and drop**: `drag: drag-drop` to pick up (not `drag: drag`, which never finds a drop target),
`folio-drag="move"` to actually move it, `folio-drop="remove|hide|accept"` on the zone. Style
`.folio-dragging` and `.folio-dragover`.

**Moving and resizing a window** needs no code: `<handle move_target="id">` and
`<handle size_target="id">`, with the target `position: absolute`.

**Data** comes from a bound object: `{{ Health }}` in text, `data-class-*`, `data-if`, `data-for`,
`data-attr-*`. There is no JavaScript.

## Where things go

```
ui/
  screens/     documents the game opens
  lib/         the game's own components, if it has any
  art/         packed atlases and the stylesheets that declare them
  fonts/       faces the game registers
  mock/        <model>.json, so a document previews with data
  theme.rcss   the game's palette
```

## Full documentation

**[docs/README.md](docs/README.md)** is the index. The pages you are most likely to need:

| | |
|---|---|
| [UI_CONVENTIONS.md](UI_CONVENTIONS.md) | **authoritative.** What exists, what is banned |
| [docs/layout.md](docs/layout.md) | flexbox and the traps |
| [docs/components_and_themes.md](docs/components_and_themes.md) | the library and the colour split |
| [docs/input_and_navigation.md](docs/input_and_navigation.md) | the `nav` rule and everything from it |
| [docs/data_binding.md](docs/data_binding.md) | driving a document from gameplay |
| [docs/testing_a_document.md](docs/testing_a_document.md) | every check and what it catches |
| [docs/accessibility.md](docs/accessibility.md) | the four annotations and the build gate |
| [docs/known_gaps.md](docs/known_gaps.md) | every known limit, when something does not work and you need to know whether it can |

## What not to do

- **Do not write custom focus logic.** Navigation is RmlUi's spatial navigation. Set `nav` and
  `tab-index` and stop.
- **Do not put colours in `folio/lib/`.** That is not where they go, and it forks the library.
- **Do not report a screen as finished without running `--strict`.** A clean-looking render proves
  almost nothing here.
- **Do not invent properties.** If it is not in UI_CONVENTIONS.md §1, it does not exist and will not
  warn.
- **Do not animate layout properties.** It is the one way to make Folio slower than the UMG it
  replaced.

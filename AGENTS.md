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
ls UI/                      # screens, lib, art, fonts, mock
cat UI/theme.rcss           # the game's palette, if it has one
ls Plugins/Folio/rcss/recipes/
```

If the project has its own conventions document, it governs taste and this one governs what exists.

### 2. Start from a recipe, not from nothing

`Plugins/Folio/rcss/recipes/` has complete working screens - `main_menu.rml`, `pause.rml`,
`confirm.rml`, `nameplate.rml`. **Copy one into `UI/screens/` and edit it.** They pass `--strict` and
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

### 4. Say what size the document was designed for

One line in the head, and whoever assigns the document in the editor gets the size filled in for them
rather than guessing it:

```html
<meta name="folio-size" content="1024x640"/>
```

Nothing measures this - `body` is almost always `100%`, so a laid-out document only ever reports the
size it was already given. Write it for any document with a size it assumes, which is every world
document, terminal, panel and nameplate. A full-screen HUD that genuinely fills whatever it is given
should leave it out.

### 5. Put colour in the right place

| | |
|---|---|
| `/folio/lib/*.rcss` | structure only, **no colour anywhere** |
| `/folio/themes/default.rcss` | the tokens and the skin |
| the game's own stylesheet | anything it disagrees with |

RCSS has no `var()`, so a theme is an override block and the later rule wins. **Never put a colour in
a component rule** - it can then only be overridden by restating the rule in full.

Use the token classes (`.c-accent`, `.t-title`, `.s-pad-m`, `.focusable`) and retune what they mean,
rather than inventing parallel ones.

### 6. Make it navigable

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

### 7. Label anything a player reads

```html
<p data-loc="start">Start Game</p>
<button class="btn focusable" id="icon" folio-label="Settings"><img sprite="icon_gear"/></button>
```

`data-loc` on every user-visible string, `data-noloc` on the ones that are not language, and
`folio-label` on any control whose visible content is not its name. Both are cheap now and expensive
to retrofit across a finished game.

`folio-label` is not only for the checker: while a screen reader is running it is what the player
hears. `uipreview --a11y-tree` prints the whole document as it will be read out, which is the fastest
way to see whether a screen makes sense to someone who cannot see it.

### 8. Verify before you say it is done

```bash
./Plugins/Folio/bin/win64/uipreview.exe UI/screens/yours.rml --root . --strict --a11y
```

**Non-zero exit means it is not right.** Nearly every RCSS mistake is a warning rather than an error,
so a check that only looks at errors passes a thoroughly broken stylesheet.

Then take a look at it:

```bash
./Plugins/Folio/bin/win64/uipreview.exe UI/screens/yours.rml --root . --screenshot out.png
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
variants. A texture the game registers is named `folio://<name>` and works anywhere a source does -
that is how a scene capture, a paper doll or a rendered item reaches a document.

**Text** is distance-field and scales freely - pick sizes for the design. Font effects reach about
0.104em, so an outline on small text gets clamped.

**Prose with emphasis in it goes in a `<text>`**, whose contents are Folio's markup rather than RML:
`You <angry>must</> go to the {tag:Place.Mountain.Temple}`. That is one translatable string with one
`data-loc`, which splitting it into spans is not. `reveal: typewriter` types it out. See
[docs/rich_text.md](docs/rich_text.md).

**Fonts are not limited to the bundled Inter and Roboto Mono.** Pick the face the design wants,
fetch the `.ttf` from [fonts.google.com](https://fonts.google.com) and drop it in `UI/fonts/` with
its licence. Do not settle for a bundled face and call it a constraint.

**Slanted and curved edges want `Supersample = 2` on the widget.** Background antialiasing is one
pixel of coverage, which is exact and still steps visibly on a shallow diagonal. Supersampling draws
the document at twice the size and scales it down; the layout is unchanged. It costs a render target
and gains text nothing, so it is a per-document choice. See [docs/hosting.md](docs/hosting.md).

Put that choice behind a console command when you wire Folio into a project: take a value of 1 to 4,
hold it on whatever owns the widget, and rebuild the document when it changes. Whether 1 or 2 is
right is a judgement about one document's edges at one resolution, and it is not a judgement anyone
can make from the source. A command makes it an A/B you can run in the game in a second. Untouched
it costs nothing at runtime, and changing it costs one document rebuild.

**Never animate `opacity` on an element with overlapping children.** It is inherited in RCSS, unlike
CSS, and it is multiplied into premultiplied vertex colours rather than compositing the subtree as
one image. Each descendant is drawn separately and overlapping quads accumulate, so a light panel
over a dark shadow blows out to white halfway through the fade and is correct at both ends. Animate
`transform` for an entry, or put the opacity on a single leaf element. Fading a group as one image
needs a `filter`, which costs a render target.

The same property carrying both a transition and an animation loses one of them, silently apart from
a warning that names neither rule. One or the other, never both.

**If the project uses CommonUI, turn off its virtual pointer.** Both default on, and both are in the
game's `DefaultEngine.ini` rather than anything Folio owns:

```ini
[ConsoleVariables]
CommonUI.ShouldVirtualAcceptSimulateMouseButton=0
CommonUI.EnableVirtualPointer=0
```

The first stops the pad's accept button becoming a left mouse click at CommonUI's software cursor,
which presses whatever the cursor is over rather than what is focused. The second stops the stick
driving that cursor, which otherwise makes a player holding only a pad generate real mouse movement.
Both present as focus bugs and neither is one. See
[docs/input_and_navigation.md](docs/input_and_navigation.md).

**A HUD sets `Interactive` false on its `UFolioWidget`.** Anything the player does not click or
navigate must not be focusable: Slate restores focus to a focusable widget on window reactivation,
so an interactive HUD steals input on alt-tab and the player cannot move. Pair it with
`SetAllUserFocusToGameViewport()` on `OnApplicationActivationStateChanged`, because anything *else*
the game put on the viewport can take focus the same way. See [docs/hosting.md](docs/hosting.md).

**Sizing already matches UMG. Leave it alone.** `Scale Follow` defaults to 1, which is literally
`GetDPIScaleBasedOnSize` for the viewport, so a document sizes exactly as a UMG widget in the same
game does. 0 is a constant pixel size and in between grows less than proportionally, but reach for
those only when a design asks.

If a HUD looks correct at 1080p and grows or shrinks twice as fast as it should elsewhere, **it is a
squared ratio, not a stylesheet problem.** A context unit is a pixel; RmlUi converts dp lengths
itself, so scaling drawn geometry by the ratio again squares it. `Folio.Scale.MatchesUMG` guards
that. Diagnose with `folio.Screens` - `drawn` must equal `px` - and with `kb.ScaleProbe`, which puts a
real UMG box on screen to measure against.

**Drag and drop**: `drag: drag-drop` to pick up (not `drag: drag`, which never finds a drop target),
`folio-drag="move"` to actually move it, `folio-drop="remove|hide|accept"` on the zone. Style
`.folio-dragging` and `.folio-dragover`.

**Moving and resizing a window** needs no code: `<handle move_target="id">` and
`<handle size_target="id">`, with the target `position: absolute`.

**Data** comes from a bound object: `{{ Health }}` in text, `data-class-*`, `data-if`, `data-for`,
`data-attr-*`. There is no JavaScript.

## Where things go

```
UI/
  screens/     documents the game opens
  lib/         the game's own components, if it has any
  art/         packed atlases and the stylesheets that declare them
  fonts/       faces the game registers
  mock/        <model>.json, so a document previews with data
  terms/       *.terms, the names the whole game shares - places, characters, items
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
| [docs/accessibility.md](docs/accessibility.md) | the annotations, the build gate and the screen reader |
| [docs/rich_text.md](docs/rich_text.md) | `<text>`, terms and the typewriter |
| [docs/known_gaps.md](docs/known_gaps.md) | every known limit, when something does not work and you need to know whether it can |
| [docs/form_controls.md](docs/form_controls.md) | the real element types, and why `<button>` is not a button |
| [docs/modals.md](docs/modals.md) | trapping focus in a dialog, which is wrong by default |
| [docs/console.md](docs/console.md) | every `folio.*` command |

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

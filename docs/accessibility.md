# Accessibility

Two halves, and only one of them exists yet.

The half that exists is everything you can get right in the markup and the stylesheet: readable text,
enough contrast, hit targets you can actually hit, a focus state you can see, and a name on every
control. `uipreview --a11y` checks all of it and fails the run under `--strict`, so it is a build
gate rather than a thing to remember.

The half that does not exist yet is screen readers. Folio draws through a single Slate widget, so
NVDA, JAWS and console TTS see one rectangle and nothing inside it. That is real work and it is
scheduled; what matters now is that the annotations it will need are already in your documents by the
time it lands. Adding `folio-label` to a control today costs nothing. Adding it to four hundred
controls later is the reason projects abandon this.

## Running the checks

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y
```

Findings are warnings, so `--strict` turns them into a failing exit code:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --strict
```

`--a11y-report <file>` writes the same findings as JSON, with the numbers rather than the prose, for
anyone tracking them over time.

## What it checks

| Code | What it means |
|---|---|
| `W-A11Y-CONTRAST` | Text below 4.5:1 against what is actually behind it. Large text is held to 3:1, as WCAG defines large. |
| `W-A11Y-TEXTSIZE` | Text under 12dp. |
| `W-A11Y-TARGET` | An interactive element smaller than 24dp in either direction. |
| `W-A11Y-NONAME` | A control with nothing to read out. |
| `W-A11Y-NOFOCUS` | Focusing the control changes nothing you can see. |
| `I-A11Y-NOBACKDROP` | Some text has no opaque background above it, so its contrast is unknowable. Informational. |

### Contrast

The backdrop is resolved properly rather than guessed: the walk goes outwards to the nearest opaque
background and composites every translucent layer in between, so a panel at `#1f1f28e0` over a darker
screen is measured as what it actually looks like. It also handles the case where the thing behind
the text is a *sibling* rather than an ancestor, which is how the component library builds a
segmented control - an absolutely positioned hit area, then the label on top of it.

When nothing in the chain is opaque, contrast is not reported. A document drawn over the world has no
knowable backdrop, and a confident wrong number is worse than no number.

### Focus

Asked by actually setting the pseudo class and re-resolving, not by reading the stylesheet. A focus
treatment can arrive from any rule that reaches the element, including one on an ancestor or a
sibling - `.radio:focus-visible + .radio-label` is normal and correct - so the whole document is
compared before and after, and a change anywhere counts.

Both `:focus` and `:focus-visible` are set, because a focus ring should only appear for keyboard and
gamepad and the shipped library styles `:focus-visible`.

Transitions are suppressed for the duration of the check. Without that, setting the pseudo class
starts an animation rather than applying the value, and every correctly styled control reads as
having no focus style at all.

## The attributes

### `folio-label`

The name of a control that has no text of its own.

```html
<button class="btn btn-icon focusable" id="icon" folio-label="Settings">
    <img class="icon" sprite="icon_settings"/>
</button>

<div class="close" folio-close="win" folio-label="Close">X</div>
```

It also wins over visible text when the two should differ. `X` reads as "Close", not as the letter X.

You do not need it when the name is already there. A control is considered named if it has
`folio-label`, its own text, text anywhere inside it, a `<label for="its-id">` in the document, or a
non-interactive sibling with text on either side - which covers a checkbox with its caption beside
it, and a segmented control with its label after the hit area.

### `folio-role`

What kind of thing this is, when the tag does not say. Read for the screen-reader work; harmless now.

```html
<div class="hp-bar" folio-role="progressbar" folio-label="Health"></div>
```

Inferred without it from the tag - `button`, `input`, `select`, `handle` - so only reach for it when
you have built a control out of `div`s.

### `folio-hidden`

Decoration. A spacer, a rule, a glow, a scanline overlay. It and everything inside it is skipped by
the checks, and will be skipped by the screen-reader tree.

```html
<div class="scanlines" folio-hidden></div>
```

Use it for things that carry no information. Do not use it to silence a finding on something a person
is meant to read - that is the one way to make the check worthless.

## Thresholds

The defaults are WCAG AA where WCAG has an opinion:

- contrast 4.5:1 for normal text, 3:1 for large (24dp, or 18.66dp bold)
- hit targets 24dp, which is WCAG 2.2 AA
- text 12dp, which is a house rule - WCAG has no minimum, and this exists because a size nobody can
  read is the failure that actually ships

MSDF text has no hinting and no subpixel antialiasing, so small text is slightly softer here than a
hinted rasteriser would give you. That is a reason to treat 12dp as a real floor rather than a
formality. See **[Text and fonts](text_and_fonts.md)**.

## What the default theme guarantees

`rcss/themes/default.rcss` and every document under `rcss/recipes/` pass with no findings. If you
build on the component library and do not override colours, you inherit that.

The two dim text tokens are both above 4.5:1 on every surface the library puts them on:

| | |
|---|---|
| `.c-muted` | the dimmer of two readable greys |
| `.c-dim` | dimmer again, still above 4.5:1 |

There is deliberately no third, darker step. A grey that fails contrast is not a text colour; if you
want something to recede further, move it rather than fading it.

## What this does not give you

An accessibility declaration for a storefront is more than contrast. Subtitles, remappable controls,
difficulty options, motion reduction and colour-blind modes are game features, not UI-toolkit
features - Folio can render them but cannot provide them.

What it does provide, once the screen-reader work lands, is that a document annotated the way this
page describes will be readable without any further markup changes.

## Where to go next

- **[Testing a document](testing_a_document.md)** — where `--a11y` fits with the rest of the checks.
- **[Themes](../UI_CONVENTIONS.md)** §13 — overriding colours without breaking the guarantee above.

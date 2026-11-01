# Accessibility checks

`uipreview --a11y` checks a document for readable text, contrast, hit targets, a visible focus state and a name on every control, and under `--strict` fails the build when any of them is wrong.

## Run the checks

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y
```

Findings are warnings, so `--strict` turns them into a failing exit code:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --strict
```

`--a11y-report <file>` writes the same findings as JSON, with the numbers, for tracking over time.

A control the checker calls named is one a screen reader reads a name for. Reading the screen aloud is the **[screen reader](screen_reader.md)**; text scale, reduced motion, high contrast, screen effects, text speed and colour vision are the **[presentation settings](presentation_settings.md)**.

## If something is wrong

**`W-A11Y-NONAME`: "nothing names this control - give it folio-label".** Nothing inside or beside the control has text. A control is already named if it has `folio-label`, its own text, text anywhere inside it, a `<label for="its-id">` in the document, or (for a form control) a wrapping `<label>` or a non-interactive sibling with text on either side, which covers a checkbox with its caption beside it and a segmented control with its label after the hit area. Otherwise add **[`folio-label`](screen_reader.md#folio-label)**.

**`W-A11Y-NOFOCUS` on a styled checkbox or segmented control that has a focus rule.** A control drawn at `opacity: 0` over its own hit area is checked like any other, because it is what a click hits and focus lands on, and its focus rule is losing the cascade. Write its focus treatment on the visible sibling, *after* whatever styles the checked state: the two have the same specificity, so a focus rule above it loses on the chip the player is most likely standing on.

**`W-A11Y-UNREACHABLE`: "only a pointer can reach this control".** It has an `onclick` and no `tab-index: auto`. Add `tab-index: auto` plus a focus style, even for something only a pointer can operate: a keyboard player still has to reach a window and be told it is there.

**`W-A11Y-COVERED`: "something is on top of this control and will take the click".** Usually an overlay faded to nothing: **an element at zero opacity is still hit tested**, so a faded-out scrim or scroll affordance eats presses on whatever is under it. Give it `pointer-events: none` except while it is visible.

**`I-A11Y-NOBACKDROP` and no contrast findings at all.** Nothing between the text and the root is opaque, so its contrast is unknowable. Expected for a document drawn over the world; judge it against the scene.

**A strobe passes `--flash-check` but looks wrong.** The check averages the whole frame, so a strobe over a quarter of the screen is diluted by the still three quarters. Judge anything near the threshold by eye as well.

**Decoration fails a check.** Mark it **[`folio-hidden`](screen_reader.md#folio-hidden)**. Never use it to silence a finding on something a person is meant to read.

## What it checks

| Code | What it means |
|---|---|
| `W-A11Y-CONTRAST` | Text below 4.5:1 against what is behind it. Large text is held to 3:1, as WCAG defines large. |
| `W-A11Y-TEXTSIZE` | Text under 12dp. |
| `W-A11Y-TARGET` | An interactive element smaller than 24dp in either direction. |
| `W-A11Y-NONAME` | A control with nothing to read out. |
| `W-A11Y-NOFOCUS` | Focusing the control changes nothing you can see. |
| `W-A11Y-UNREACHABLE` | A control only a pointer can reach, on a screen that otherwise navigates. |
| `W-A11Y-COVERED` | Something the player cannot move is on top of a control and takes the click. |
| `W-A11Y-FLASH` | Under `--flash-check`, flashing over three times a second. |
| `W-A11Y-CLIPPED` | Something is bigger than the box it is in, and the box hides what does not fit. |
| `W-A11Y-CVD` | Under `--simulate`, text that only passes contrast because of a hue difference that vision does not have. |
| `I-A11Y-NOBACKDROP` | Some text has no opaque background above it, so its contrast is unknowable. Informational. |
| `I-A11Y-FIXEDTEXT` | Under `--text-scale`, text the setting could not move. Informational: some text is right not to move. |

`--text-scale` and `--simulate` are covered under **[presentation settings](presentation_settings.md)**.

### Contrast

The walk goes outwards to the nearest opaque background and composites every translucent layer in between, so a panel at `#1f1f28e0` over a darker screen is measured as it looks. It also handles a backdrop that is a *sibling* rather than an ancestor, as in the library's segmented control (an absolutely positioned hit area with the label on top).

### Reachability

`W-A11Y-UNREACHABLE` is asked only on a document where *something* takes focus; a HUD with nothing focusable is skipped.

### Covered controls

`W-A11Y-COVERED` asks the context what the pointer would hit at the middle of each control (the same code a click goes through) and reports anything that answers with something else.

Not reported: a control drawn at `opacity: 0` over its own hit area (the styled-checkbox pattern), and something the player can drag off, such as a floating window or a draggable icon.

It checks one static frame: a control scrolled out of view is skipped, and an overlay that covers something only in another state is not caught. Run it in the states that matter.

### Flashing

```bash
./bin/win64/uipreview.exe UI/screens/alarm.rml --root . --flash-check --filmstrip-frames 90
```

Renders the frames and measures how often the whole screen changes brightness. More than three flashes a second is a seizure risk (WCAG 2.3.1) and a question on platform submission forms.

It measures the **whole frame's** average luminance rather than the ten-degree field the standard defines. That is safe for false positives (a blinking caret cannot trip it) and risky for false negatives.

### Focus

Checked by setting the pseudo class and re-resolving. A focus treatment can come from a rule on an ancestor or a sibling (`.radio:focus-visible + .radio-label` is normal), so the whole document is compared before and after, and a change anywhere counts.

Both `:focus` and `:focus-visible` are set, because the shipped library styles `:focus-visible`.

Transitions are suppressed during the check; otherwise every correctly styled control reads as having no focus style.

## Thresholds

The defaults are WCAG AA where WCAG has an opinion:

- contrast 4.5:1 for normal text, 3:1 for large (24dp, or 18.66dp bold)
- hit targets 24dp, which is WCAG 2.2 AA
- text 12dp, a house rule (WCAG has no minimum)

MSDF text has no hinting and no subpixel antialiasing, so small text is slightly softer than a hinted rasteriser gives. Treat 12dp as a real floor. See **[Text and fonts](text_and_fonts.md)**.

## What the default theme guarantees

`rcss/themes/default.rcss` and every document under `rcss/recipes/` pass with no findings. If you build on the component library and do not override colours, you inherit that.

The two dim text tokens are both above 4.5:1 on every surface the library puts them on:

| | |
|---|---|
| `.c-muted` | the dimmer of two readable greys |
| `.c-dim` | dimmer again, still above 4.5:1 |

No third, darker step exists. A grey that fails contrast is not a text colour; to make something recede further, move it rather than fading it.

## What this does not give you

Subtitles, remappable controls, difficulty options and anything CVAA touches are game features: Folio can render them but not provide them.

## Where to go next

- **[Testing a document](testing_a_document.md)**, where `--a11y` fits with the rest of the checks.
- **[Themes](../UI_CONVENTIONS.md)** §13: overriding colours without breaking the guarantee above.

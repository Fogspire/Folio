# Accessibility

Two halves. The first is everything you can get right in the markup and the stylesheet: readable
text, enough contrast, hit targets you can actually hit, a focus state you can see, and a name on
every control. `uipreview --a11y` checks all of it and fails the run under `--strict`, so it is a
build gate rather than a thing to remember.

The second is the screen reader. Folio draws through a single Slate widget, and NVDA, JAWS and
console TTS read Unreal's widget tree - so while one of them is running, the document is walked into
a tree of Slate widgets, one per thing worth announcing, and that is what gets read. Nothing is built
while nothing is listening.

Both halves read the same walk. A control the checker calls named is a control a screen reader reads
a name for, and `--a11y-tree` prints exactly what the engine will build.

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

A control drawn at `opacity: 0` over its own hit area - the styled checkbox, the segmented control,
anything where the input is invisible and the thing you see is a sibling - is checked like any other
control, because it is still what a click hits and what focus lands on. The focus treatment for one
of those has to be written on the visible sibling, and it has to come *after* whatever styles the
checked state: the two have the same specificity, so a focus rule written above it loses on the chip
the player is most likely to be standing on.

## The screen reader

### What happens

While the platform reports that something is listening, every screen document walks itself once per
frame *in which it changed* and produces a tree of hit-test-invisible, unfocusable Slate widgets: one
per group, control, list, item and run of text. Each carries a name, a role, a value, a checked or
disabled state and the bounds of the element it stands for. Unreal builds its own accessibility tree
from those the way it does for any UI, and the platform reads it.

Moving through a document with the pad or the keyboard tells the platform what was moved onto, so a
menu reads itself out as it is navigated. Activating a control from the screen reader presses the
element, focus first, so anything a document does on focus happens for that player too.

Nothing exists while nothing is listening: no widgets, no walk, one branch a frame.

### What it costs

The walk is the whole document, so it is proportional to the document rather than to what changed.
It runs on a frame the document changed and not otherwise, which for a settings screen is when
someone touches it. A HUD that animates every frame walks every frame - measure it with `stat Folio`
before putting a screen reader and a busy HUD together.

### Reading the tree without a screen reader

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y-tree
```

```
group "Settings"
  group "Audio"
    text "Audio"
    slider "Master volume" = 60
    combobox "Output device" = Speakers
    checkbox "Subtitles" [checked]
  button "Apply"
  button "Revert" [disabled]
  button "Close"
```

`--a11y-tree-report <file>` writes the same as JSON, with bounds, ranges and element paths, which is
what a test diffs. `Tools/uipreview/tests/a11y_tree.rml` is checked against a committed transcript by
`scripts/verify.sh`, so a change to what a document reads as is a failing test rather than something
noticed by ear.

In a running game, `folio.Accessibility.Dump` prints the same thing for every screen document - from
the live tree when a screen reader is running, and from the document itself when one is not.
`folio.Accessibility.Force 1` builds the tree with nothing listening, which is how you measure it.

### What ends up in the tree

- Controls, always: anything with a control tag, an `onclick`, a `folio-drag`, a `folio-close`, or a
  `tab-index: auto`.
- Text, wherever an element has words of its own.
- Containers, only when named with `folio-label`. An unnamed panel is layout, and a tree full of
  unnamed panes is how a screen reader becomes something people switch off.
- Nothing that is `folio-hidden`, hidden by `visibility`, collapsed to no size, or faded out - except
  the invisible-input case above, which survives because it is still what a click hits.
- Nothing RmlUi generated for itself: a scrolling panel's scrollbars are not five things to walk past.

A caption that has already been spent naming a control is not repeated as text of its own. Otherwise
a checkbox and its label read as "Subtitles, check box, checked. Subtitles."

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
`folio-label`, its own text, text anywhere inside it, a `<label for="its-id">` in the document, or -
for a form control - a wrapping `<label>` or a non-interactive sibling with text on either side,
which covers a checkbox with its caption beside it and a segmented control with its label after the
hit area.

A caption pointed at a control is spent naming it and is not read again. A `<label>` *wrapped around*
one is read twice, once as the name and once as itself, because taking it out of the tree would take
the row it wraps with it - which is what a browser does with the same markup.

On a container it does one more thing: it is what puts that container in the tree at all, and what
names it. A dialog, a settings group or a list wants one; a flex row does not.

### `folio-role`

What kind of thing this is, when the tag does not say.

```html
<div class="hp-bar" folio-role="progressbar" folio-label="Health"></div>
```

Inferred without it from the tag - `button`, `input` and its `type`, `select`, `textarea`, `progress`,
`img`, `a`, `handle` - so only reach for it when you have built a control out of `div`s.

| Value | Read as |
|---|---|
| `group`, `dialog`, `region` | a pane |
| `text`, `label`, `heading` | text |
| `image` | an image |
| `button` | a button |
| `link` | a link |
| `checkbox`, `radio` | a check box, with its state |
| `combobox`, `select` | a combo box, with its selection |
| `slider` | a slider, with its value and range |
| `progressbar` | a slider, read-only, with its value |
| `textbox` | an edit field, with its contents |
| `list`, `listbox` | a list |
| `listitem`, `option` | an item in one |

There is no heading, region or landmark role that survives to the platform: Unreal's accessibility
layer has nowhere to put them, and a role nothing can announce is a promise the toolkit does not
keep. A progress bar becomes a slider for the same reason - it is the only thing in that list that
carries a value and a range, and "slider, 40" is better than silence.

### `folio-description`

A sentence read after the name, for the consequence a name cannot carry.

```html
<button folio-label="Delete save" folio-description="This cannot be undone">Delete</button>
```

### `folio-hidden`

Decoration. A spacer, a rule, a glow, a scanline overlay. It and everything inside it is skipped by
the checks and by the screen-reader tree.

```html
<div class="scanlines" folio-hidden></div>
```

Use it for things that carry no information. Do not use it to silence a finding on something a person
is meant to read - that is the one way to make the check worthless.

### `folio-announce`

A line that is read out whenever its text changes, wherever the reader happens to be.

```html
<div class="status" folio-announce data-attr-value="model.status">{{ model.status }}</div>
```

For the things a screen reader would otherwise never reach: a save completing, a warning appearing in
a corner, a round starting. A reader only reads what it is standing on, and nobody stands on a status
line.

It announces on change, not on arrival: the tree built when a screen reader starts does not read out
every announcing element on screen at once.

Game code with no element to hang it on calls `FFolioAccessibilityTree::Announce`, which is the same
path. `folio.Accessibility.Say <text>` is that from the console.

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

A document in the world is not exposed. `UFolioWorldDocument` and `UFolioMarkerDocument` draw into
the scene rather than into a Slate widget, so there is no widget tree for a screen reader to walk and
no useful screen position to report if there were. A nameplate over a character is not a control; a
terminal the player walks up to and uses is, and the answer for one of those is to put its interface
on a screen document when it is being used.

An accessibility declaration for a storefront is also more than a readable tree. Subtitles, remappable
controls, difficulty options, motion reduction and colour-blind modes are game features, not
UI-toolkit features - Folio can render them but cannot provide them.

## Where to go next

- **[Testing a document](testing_a_document.md)** — where `--a11y` fits with the rest of the checks.
- **[What Folio does not do yet](known_gaps.md)** — the limits of the screen-reader support.
- **[Themes](../UI_CONVENTIONS.md)** §13 — overriding colours without breaking the guarantee above.

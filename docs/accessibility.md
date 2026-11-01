# Accessibility

Three parts. The first is what you get right in the markup and the stylesheet: readable text, contrast, hit targets, a visible focus state, and a name on every control. `uipreview --a11y` checks all of it and fails the run under `--strict`, so it is a build gate.

The second is the screen reader. NVDA, JAWS and console TTS read Unreal's widget tree, so while one is running the document is walked into a tree of Slate widgets, one per thing worth announcing. Nothing is built while nothing is listening. A player with no screen reader gets the same thing spoken by the game itself.

The third is the presentation settings: larger text, less movement, higher contrast. Those are three lines of game code and a stylesheet written to expect them.

The first two read the same walk: a control the checker calls named is one a screen reader reads a name for, and `--a11y-tree` prints what the engine builds.

## Why a UI toolkit ships this

Four pressures, of different force.

**Store metadata, self-declared.** Xbox, PlayStation and Steam take an accessibility feature declaration on the product page (screen reader support, subtitle options, remappable controls, colour-blind modes, no-flashing). Nobody audits it up front and nothing blocks a release, but the declaration is public and wrong entries are found by the people who depend on them. Nintendo has historically been the least developed here.

**Certification, mandatory but narrow.** Platform cert asks a handful of specific accessibility questions rather than judging whether a UI is usable. The one with teeth is the US **CVAA**: text and voice chat have to be usable by players with disabilities, and it appears in cert. Microsoft also publishes the Xbox Accessibility Guidelines and a validation service, guidance rather than pass or fail for most titles.

**Regulation, the direction of travel.** The **European Accessibility Act** came into force in June 2025. Whether game *content* falls in scope is contested; the storefront, the purchase flow and the account and support services around a game are clearer targets.

**Reputation.** Specialist outlets and player communities review this, and menus that cannot be read out are the most cited failure.

Folio provides the plumbing (a readable tree, contrast and target gates, text scale, reduced motion, a high-contrast theme) and makes the checks a build gate. Subtitles, remapping, difficulty options and anything CVAA touches are game features Folio can render but not supply.

- The declaration you can honestly make from Folio alone is "screen reader support in menus", "text size options", "reduced motion" and "high contrast", plus the visual ones the checker gates. Everything else on a store form is yours to build.
- Specifics per platform move constantly. Read the current requirements on each platform's developer portal before filling in a submission form.

## Running the checks

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y
```

Findings are warnings, so `--strict` turns them into a failing exit code:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --strict
```

`--a11y-report <file>` writes the same findings as JSON, with the numbers, for tracking over time.

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

### Contrast

The walk goes outwards to the nearest opaque background and composites every translucent layer in between, so a panel at `#1f1f28e0` over a darker screen is measured as it looks. It also handles a backdrop that is a *sibling* rather than an ancestor, as in the library's segmented control (an absolutely positioned hit area with the label on top).

When nothing in the chain is opaque, contrast is not reported: a document drawn over the world has no knowable backdrop.

### Reachability

A control with an `onclick` and no `tab-index: auto` can be pressed with a mouse and reached by nothing else. That is `W-A11Y-UNREACHABLE`, asked only on a document where *something* takes focus; a HUD with nothing focusable is skipped.

The fix is usually `tab-index: auto` plus a focus style, even for something only a pointer can operate: a keyboard player still has to reach a window and be told it is there.

### Covered controls

A control can be named, focusable, big enough and still unpressable, because something is drawn on top of it. `W-A11Y-COVERED` asks the context what the pointer would hit at the middle of each control (the same code a click goes through) and reports anything that answers with something else.

The usual case is an overlay faded to nothing: **an element at zero opacity is still hit tested**, so a faded-out scrim or scroll affordance eats presses on whatever is under it. Give it `pointer-events: none` except while it is visible.

Not reported: a control drawn at `opacity: 0` over its own hit area (the styled-checkbox pattern), and something the player can drag off, such as a floating window or a draggable icon.

It checks one static frame: a control scrolled out of view is skipped, and an overlay that covers something only in another state is not caught. Run it in the states that matter.

### Flashing

```bash
./bin/win64/uipreview.exe UI/screens/alarm.rml --root . --flash-check --filmstrip-frames 90
```

Renders the frames and measures how often the whole screen changes brightness. More than three flashes a second is a seizure risk (WCAG 2.3.1) and a question on platform submission forms.

It measures the **whole frame's** average luminance rather than the ten-degree field the standard defines. That is safe for false positives (a blinking caret cannot trip it) and risky for false negatives: a strobe over a quarter of the screen is diluted by the still three quarters. Judge anything near the threshold by eye as well.

### Focus

Checked by setting the pseudo class and re-resolving. A focus treatment can come from a rule on an ancestor or a sibling (`.radio:focus-visible + .radio-label` is normal), so the whole document is compared before and after, and a change anywhere counts.

Both `:focus` and `:focus-visible` are set, because the shipped library styles `:focus-visible`.

Transitions are suppressed during the check; otherwise every correctly styled control reads as having no focus style.

A control drawn at `opacity: 0` over its own hit area (the styled checkbox, the segmented control) is checked like any other, because it is what a click hits and focus lands on. Write its focus treatment on the visible sibling, *after* whatever styles the checked state: the two have the same specificity, so a focus rule above it loses on the chip the player is most likely standing on.

## The screen reader

### Two players, one tree

**Someone running a screen reader** (NVDA or JAWS on Windows, a narrator on a console) needs nothing switched on. The platform asks for the accessibility tree, Folio builds one, and that software reads it with the player's own settings. This route matters most.

**Someone with no screen reader** gets nothing from that. On a console there is often nothing to run; on Windows many players who need menus read do not run one. `SetScreenReader(true)` builds the same tree and speaks the focused control through the platform's own voice. One back end ships, SAPI, which is part of Windows: no library and no voice data to ship. See [speaking on other platforms](#speaking-on-other-platforms).

Put that setting in your options menu, off by default, saved and applied before the first menu opens. `UFolioAccessibilityLibrary` has it, along with speech rate and volume, and `IsSpeechAvailable` so a platform with no voice hides the row.

It says name, then role, then state, then value ("Master volume down, button", "Subtitles, check box, checked"), with the enclosing group named on the way in and not repeated after that: "Audio, master volume down, button".

### Reading without moving

Focus does not reach a heading, a paragraph, a warning, a score, or the label above the field you are in, so a player who hears only what focus lands on hears a menu with its words removed. A screen reader solves this with a second cursor the player drives independently; this is Folio's.

```cpp
UFolioAccessibilityLibrary::ReviewNext();       // and Previous, First, Last
UFolioAccessibilityLibrary::ReviewRepeat();     // that again
UFolioAccessibilityLibrary::ReviewAll();        // from here to the end of the screen
UFolioAccessibilityLibrary::ReviewStop();       // what any other key should also do
UFolioAccessibilityLibrary::ReviewActivate();   // press what the cursor is on
UFolioAccessibilityLibrary::ReviewMoveFocusHere();
```

Bind next, previous, repeat and read-all to something reachable with one hand; the rest are conveniences. Each returns what was said, so a game can put the same line on screen.

The cursor covers every live document, newest first, so a modal is read before the screen behind it, **including a document drawn in the world**, which no platform screen reader can see. It follows focus, and steps over layout containers that have nothing to say.

`ReviewNextHeading` and `ReviewPreviousHeading` jump between headings, so a long page is read without walking every line. They need the document to use `<h1>`-`<h6>` or `folio-role="heading"`.

`folio.Accessibility.Review [list|next|previous|first|last|heading|prevheading|repeat|all|stop|activate|focus]` drives it from the console before anything is bound.

### How much it says

```cpp
UFolioAccessibilityLibrary::SetReaderVerbosity(EFolioReaderVerbosity::Low);
```

| | |
|---|---|
| `Low` | The name, the state and the value. Nothing else. |
| `Normal` | Adds what kind of control it is, and the panel it is in on the way into one. The default. |
| `High` | Adds the `folio-description`. |

Someone who has learned a screen wants "Master volume, 60"; someone meeting it wants to be told it is a slider and what it does.

### What it does when it falls behind

Everything the reader says goes through one queue Folio owns, because a line handed to a voice cannot be taken back, and a player holding a key on a slider produces one line per frame.

- **A focus change is assertive.** It drops the queue and cuts off what is speaking, because everything waiting was about a control the player has already left.
- **An announcement is polite, and carries a channel.** A later line on the same channel *replaces* an earlier one still waiting, so a value that keeps changing leaves one pending line rather than forty, and what gets read is the number it ended on.
- **A polite line that has waited more than four seconds is dropped unsaid.** By then the screen has moved on.
- **A read-all queues and never expires.**

`folio.Accessibility.Verbosity` reports how many lines are waiting; watch it if a screen feels like it is talking about the past.

### Typing

A text field's contents are one accessible value, and a value changing is not an event a screen reader announces, so a player typing would hear nothing. The game's own reader echoes as you type:

- each character as it goes in, with punctuation named, because a voice handed a bare full stop says nothing
- a pasted or otherwise multi-character run as words rather than one symbol at a time
- what was deleted, as it goes
- "blank" when the field empties, which is the one state a player cannot infer
- the selection when it changes, and the text the caret moves over

A password field says stars, never characters.

Only the game's own reader does this. A platform reader has its own character echo.

### Inside a field

Reading a field back, for correcting:

```cpp
UFolioAccessibilityLibrary::ReadEditWord();    // the word the caret is in
UFolioAccessibilityLibrary::ReadEditLine();    // the line it is on
UFolioAccessibilityLibrary::SpellEditWord();   // "V, e, g, a"
```

The caret does not move. Bind these beside the review keys.

How much punctuation any of it names is its own setting:

```cpp
UFolioAccessibilityLibrary::SetPunctuation(EFolioPunctuation::All);
```

`None` names none of it, `Some` names a single keystroke and reads a run as words, `All` names every symbol wherever it appears. Someone entering a file path wants `All`; someone entering their name wants `None`. `Some` is the default.

### The reader's own words

"check box", "not checked", "unavailable", the name of the comma key: all of it is `NSLOCTEXT` under the `FolioAccessibility` namespace in `FolioAccessibilityText.cpp`. Unreal's ordinary source gather finds it, so it lands in **your project's** localisation target rather than the one the markup goes to.

Nothing is assembled by concatenation: "deleted a" is a format string with an argument, because other languages order it differently. Numbers go through `FText::AsNumber`, so a slider reads its value the way the culture writes one.

### Speaking on other platforms

`Folio::FSpeech` takes a back end. SAPI ships because it is part of Windows; every other platform's speech API sits behind an SDK this plugin cannot see, so install one from a module that can:

```cpp
// From your platform module's StartupModule, before the reader is ever turned on.
Folio::FSpeech::SetBackend(MakeUnique<FMyPlatformVoice>());
```

`Folio::ISpeechBackend` is six functions (start, stop, say, silence, rate, volume). Only `Say` has a rule: a queued line waits for whatever is being spoken, an interrupting one drops it. A back end with no queue of its own should speak immediately rather than drop the line.

Rate and volume live above the back end, so a player's choice survives the reader being toggled and a back end being swapped.

The `Folio.Accessibility.Speech` test installs a back end that records what it was told to say, so what a menu reads out is an assertion.

### What happens

While either of those is true, every screen document produces a tree of hit-test-invisible, unfocusable Slate widgets: one per group, control, list, item and run of text. Each carries a name, a role, a value, a checked or disabled state and the bounds of its element. Unreal builds its accessibility tree from those, and the platform reads it.

Moving through a document with the pad or the keyboard tells the platform what was moved onto, so a menu reads itself out as it is navigated. Activating a control from the screen reader focuses, then presses, the element.

Nothing exists while nothing is listening: no widgets, no walk, one branch a frame.

### What it costs

Two costs, separated because a document that changed has usually not changed what it *means*.

**Following the elements** happens on every frame the document changed: one box read per node, no strings, no allocation. A HUD animating a bar pays only this.

**Walking the document** happens at most ten times a second (`folio.Accessibility.WalkInterval`), and immediately when a screen reader starts or focus lands somewhere the tree has not seen, so a dialog that opens is announced the next frame. The walk is proportional to the document; `stat Folio` reports it as *Accessibility walk*, and nothing while nobody is listening.

### Modals

A dialog's background is out of the tree. The signal is the pair of properties [docs/modals.md](modals.md) already tells you to write:

```css
.screen.is-behind-modal { focus: none; pointer-events: none; }
```

`folio-inert` says the same thing outright, for a game that traps focus its own way. Either property alone is ordinary (`pointer-events: none` is how a decoration stops eating clicks), so both are required.

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

`--a11y-tree-report <file>` writes the same as JSON, with bounds, ranges and element paths, for a test to diff. `scripts/verify.sh` checks `Tools/uipreview/tests/a11y_tree.rml` against a committed transcript.

In a running game, `folio.Accessibility.Dump` prints the same for every screen document, from the live tree when a screen reader is running and from the document otherwise. `folio.Accessibility.Force 1` builds the tree with nothing listening, for measuring it, and `folio.Accessibility.Reader 1` turns the game's own reader on without an options menu. With `LogFolio` at Verbose, every sentence it speaks is logged.

### Changing what a screen says

A correct tree does not always read well. Read the transcript; every line you disagree with is one attribute on one element:

| What is wrong with the line | What fixes it |
|---|---|
| The words are wrong, or it is a symbol | `folio-label="..."` |
| It needs a consequence said after the name | `folio-description="..."` |
| It is read as the wrong kind of thing | `folio-role="..."` |
| It should not be read at all | `folio-hidden` |
| It changes and nobody would ever land on it | `folio-announce` |
| It is a switch that reads as its own state | `folio-role="checkbox"` and `folio-checked` |

For a whole game, dump the tree for every document into one file, edit the lines you want changed, and work back to the markup.

```bash
for doc in UI/screens/*.rml; do
    echo "== $doc"
    ./Plugins/Folio/bin/win64/uipreview.exe "$doc" --root . --a11y-tree
done > accessibility-transcript.txt
```

Read it out loud. A line that is accurate and clumsy ("ON, button", "minus, button", the same panel name before every row) is one a player hears a hundred times.

### What ends up in the tree

- Controls, always: anything with a control tag, an `onclick`, a `folio-drag`, a `folio-close`, or a `tab-index: auto`.
- Text, wherever an element has words of its own.
- Containers, only when named with `folio-label`. An unnamed panel is layout.
- Nothing that is `folio-hidden`, hidden by `visibility`, collapsed to no size, or faded out, except the invisible-input case above, which is still what a click hits.
- Nothing RmlUi generated for itself, such as a scrolling panel's scrollbars.

A caption already spent naming a control is not repeated as text of its own, so a checkbox and its label do not read as "Subtitles, check box, checked. Subtitles."

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

A control is already named if it has `folio-label`, its own text, text anywhere inside it, a `<label for="its-id">` in the document, or (for a form control) a wrapping `<label>` or a non-interactive sibling with text on either side, which covers a checkbox with its caption beside it and a segmented control with its label after the hit area.

A caption pointed at a control is spent naming it and is not read again. A `<label>` *wrapped around* one is read twice, as the name and as itself, because taking it out of the tree would take the row with it (a browser does the same).

On a container it puts that container in the tree and names it. A dialog, a settings group or a list wants one; a flex row does not.

### `folio-role`

What kind of thing this is, when the tag does not say.

```html
<div class="hp-bar" folio-role="progressbar" folio-label="Health"></div>
```

Without it the role is inferred from the tag (`button`, `input` and its `type`, `select`, `textarea`, `progress`, `img`, `a`, `handle`), so use it only for a control built out of `div`s.

| Value | Read as |
|---|---|
| `group`, `dialog` | a pane |
| `region`, `landmark` | a named part of the screen |
| `heading` | a title, which the review cursor can jump between |
| `text`, `label` | text |
| `image` | an image |
| `button` | a button |
| `link` | a link |
| `checkbox`, `radio` | a check box, with its state |
| `combobox`, `select` | a combo box, with its selection |
| `slider` | a slider, with its value and range |
| `progressbar` | a slider, read-only, with its value |
| `textbox` | an edit field, with its contents |
| `list`, `listbox` | a list |
| `listitem`, `option` | an item in one, with its position |
| `table`, `grid` | a table |
| `row` | a row, with its position |
| `cell`, `gridcell` | a cell, under its column's name |

`<h1>` to `<h6>` are headings without being told, and are what the review cursor jumps between.

Heading and region do not survive to a *platform* screen reader. Unreal's accessibility layer has no role for either, so they arrive as text and as a pane. The game's own reader says the word, and heading navigation works either way. A progress bar becomes a slider because that is the only role in the list carrying a value and a range.

### Lists and tables

An item in a list says how far through it is: "Sector 2, item, 2 of 7". It is counted after the walk rather than from the markup, so an item that is hidden, faded out or inert is not one of seven.

A table is read with its column names, taken from the first row made of `<th>`:

```html
<table>
    <tr><th>Weapon</th><th>Damage</th></tr>
    <tr><td>Rifle</td><td>32</td></tr>
</table>
```

reads as "row, 2 of 2. Weapon, Rifle, 1 of 2. Damage, 32, 2 of 2." Without the header row it is a run of numbers. `<h1>`-`<h6>`, `<table>`, `<tr>`, `<td>` and `<th>` all infer their roles.

A table does not reach a *platform* reader as a table (Unreal has no type for one), but the column names are part of what each cell says.

### `folio-checked`

The state of a check box or a radio built out of `div`s, where no input carries it.

```html
<span class="switch focusable" folio-role="checkbox" folio-label="Invert look"
      data-attr-folio-checked="bInvertY" data-class-yes="bInvertY">{{ bInvertY ? 'ON' : 'OFF' }}</span>
```

Anything but `false` or `0` is on. A real `<input type="checkbox">` carries its own state and needs none of this.

`ON` and `OFF` are a *state*, not a name, so a switch whose visible text is its state needs a `folio-label` and a role. Without them a screen reader says "ON, button".

### `folio-description`

A sentence read after the name, for the consequence a name cannot carry.

```html
<button folio-label="Delete save" folio-description="This cannot be undone">Delete</button>
```

### `folio-hidden`

Decoration. A spacer, a rule, a glow, a scanline overlay. It and everything inside it is skipped by the checks and by the screen-reader tree.

```html
<div class="scanlines" folio-hidden></div>
```

Use it for things that carry no information, never to silence a finding on something a person is meant to read.

### `folio-announce`

A line read out whenever its text changes, wherever the reader is.

```html
<div class="status" folio-announce data-attr-value="model.status">{{ model.status }}</div>
```

For what a reader would otherwise never reach: a save completing, a warning in a corner, a round starting.

It announces on change, not on arrival, so a screen reader starting does not read every announcing element at once.

Each announcing element has its own place in the queue, so a value that changes forty times leaves one line waiting and reads the number it ended on. `folio-announce="assertive"` cuts in over whatever is being said: for a warning, never for a score.

```html
<div class="alert" folio-announce="assertive" data-attr-value="model.alert">{{ model.alert }}</div>
```

Use it for a value nobody can navigate to. In a stepper row (a label, a minus, a number, a plus) the number is not focusable, so `folio-announce` on it says what pressing plus made it.

Game code with no element to hang it on calls `UFolioAccessibilityLibrary::Announce`, the same path. `folio.Accessibility.Say <text>` is that from the console.

## Reading a screen from game code

The semantic walk is a machine-readable description of what is on a screen and what the player is on, keyed by name rather than by element path, so it survives the screen being rebuilt.

```cpp
TArray<FString> Screen = UFolioAccessibilityLibrary::DescribeScreen();
FString On = UFolioAccessibilityLibrary::GetFocusedControl();   // "Master volume, slider, 60"
```

**Neither needs anything turned on.** They walk the document, not the platform widgets, so there is no setting and no cost while nobody calls them. `DescribeScreen` costs one walk per open document, so call it on demand rather than every frame; `GetFocusedControl` is cheap enough to poll.

Uses:

- **UI tests driven by name.** "Focus Master volume, press right four times, assert it reads 80" survives a redesign that renames every class in the document.
- **Telemetry.** Which controls players reach, which settings nobody finds, where they stop.
- **Reacting to what the player is doing.** An NPC that notices someone hovering *Quit* for eight seconds, or turning the difficulty down mid-run, needs no wiring per menu.
- **Localisation QA.** It is every user-visible string in reading order; dump it per culture and diff.
- **Handing a screen to something that has to reason about it**: an agent operating the menus, or a support tool walking a player through a settings change.

### Holding the platform tree open

The Slate widgets that NVDA, a console narrator or a UI Automation harness walk exist only while something is listening. A harness driving the game through the platform's accessibility API should request them rather than use the debug cvar:

```cpp
// Native code, so it cannot be left held by an early return.
FFolioDocumentTreeRequest Request(TEXT("automation"));
```

`AddDocumentTreeRequest` / `RemoveDocumentTreeRequest` are the Blueprint pair. Requests are counted by reason, so two systems asking do not cancel one another, and `folio.Accessibility.Dump` names whatever is holding the tree open, since a request nobody released is otherwise a cost with no symptom.

## Presentation settings

Three settings that change how a screen looks and nothing about what it means. All three are global, reach documents opened afterwards, and are off by default and never inferred from the operating system.

```cpp
UFolioAccessibilityLibrary::SetTextScale(1.5f);      // 0.5 to 3, 1 being as authored
UFolioAccessibilityLibrary::SetReducedMotion(true);
UFolioAccessibilityLibrary::SetHighContrast(true);
```

`folio.Accessibility.TextScale`, `.ReducedMotion` and `.HighContrast` do the same from the console.

### Text scale

Applied by setting the root font size, so it reaches every rule written in `rem` and no rule written in `dp`. The shipped theme sizes all of its type in `rem` against one `dp` root:

```css
body       { font-size: 20dp; }   /* the root, and the only dp size in the theme */
.t-title   { font-size: 1.4rem; }
.t-body    { font-size: 1rem; }
```

Size your own type the same way. Preview the result and find what it could not move:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --text-scale 1.5
```

`I-A11Y-FIXEDTEXT` names each piece of text the scale did not move, measured rather than inferred from the unit. It is informational because some text is right not to move: a letter inside a key cap, a logo, a version string. `W-A11Y-CLIPPED` names anything that stopped fitting, such as a fixed-height row whose label now overlaps the one under it. That one is a warning and fails `--strict`.

Every shipped recipe is checked at 150% as well as at 100%.

### Reduced motion

Vestibular disorders make large movement unpleasant, and a menu that slides, scales and parallaxes is the usual trigger.

It is a theme, `reduced-motion`, declared in `rcss/folio.rcss` (the user-agent stylesheet every document links), so it reaches a game's own rules and elements the theme has never heard of:

```css
@media (theme: reduced-motion)
{
    * { transition: none; animation: none; }
}
```

It also turns **smooth scrolling** off, which is not a transition: RmlUi glides a focused item into view from its own controller.

What is left is movement written from code every frame. `UFolioHudMotion` reads the setting and snaps to rest; anything else you drive yourself should read `UFolioAccessibilityLibrary::IsReducedMotionOn` and do the same. A video and a render-target surface are outside it either way.

A shader decorator has no clock of its own; it moves only through a stylesheet animating the `decorator` property, which stops with everything else.

A `<text>` typing itself out is deliberately outside it; see below.

`uipreview --reduced-motion` turns on the same theme, for judging a screen with everything still.

### High contrast

`rcss/themes/default.rcss` carries a `high-contrast` block. The focus ring gains width as well as contrast, since a 2dp ring is often invisible to the players who need this, and disabled controls stop relying on opacity alone.

A screen that writes its own colours has to answer the setting itself, because a theme cannot reach a rule inside a document's own `<style>`:

```css
@media (theme: high-contrast)
{
    body { background-color: #000000; }
    h1   { color: #c6c6d2; }
}
```

Check one at the high-contrast ratio:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --theme high-contrast --a11y-contrast 7
```

### Screen effects

The washes and flashes a game paints over the whole view: a damage vignette, a burning tint, a hit flash. They are a legibility, motion and photosensitivity problem at once, and whoever needs one gone usually needs all of them gone.

Full-screen layers are the most expensive shape a document has: bake them and draw textures rather than using `box-shadow` or `filter` at screen size. See **[performance](performance_profiling.md)**.

Folio cannot tell a vignette from a panel, so **mark the layers**:

```html
<div id="base"    class="edge" folio-screen-effect/>
<div id="burning" class="edge" folio-screen-effect/>
<div id="hit"     class="edge" folio-screen-effect/>
```

```cpp
UFolioAccessibilityLibrary::SetScreenEffects(false);
```

On by default; nothing disappears from a game that has marked nothing. Mark what covers the view and reads as an effect; leave anything a player needs to see. `examples/screen_edge.rml` is the worked example, `folio.Accessibility.ScreenEffects` is the console, and `uipreview --no-screen-effects` previews it.

Pair it with `--flash-check`: the layer that trips the flash limit usually most needs marking.

### Text speed

Its own setting, not part of reduced motion:

```cpp
UFolioAccessibilityLibrary::SetTextSpeed(2.0f);      // 1 is as authored. Clamped to 0.1 - 10
UFolioAccessibilityLibrary::SetInstantText(true);    // every line arrives whole
```

Somebody can need menus still and dialogue paced, or instant text and every transition intact. A typewriter is also small, local, predictable movement, unlike what triggers a vestibular response.

**The multiplier divides the whole schedule** (`reveal-rate`, an authored `{delay:0.2}` and the punctuation beats move together), so a writer's pacing survives. Instant text skips rather than never starting, so `revealcomplete` still fires and whatever was waiting on the line still runs.

`uipreview --text-speed 4` and `--instant-text` preview both; `folio.Text.Speed` is the console.

### Colour vision

Around one man in twelve has some colour vision deficiency, the most common visual difference a UI meets, and the person who drew the screen cannot see it fail.

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --screenshot out.png \
    --simulate deuteranopia
```

Every capture is redrawn as that vision sees it, and the contrast pass runs again on the simulated colours. `W-A11Y-CVD` reports the pairs that pass as drawn and fail as seen. `protanopia`, `deuteranopia`, `tritanopia` and `achromatopsia` are the four, at full severity, using the Machado 2009 transforms.

No tool checks meaning carried by colour alone, such as a red bar and a green bar. Add a shape, a label or a position.

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

A document in the world is not exposed to a *platform* screen reader. `UFolioWorldDocument` and `UFolioMarkerDocument` draw into the scene rather than into a Slate widget, so there is no widget tree for NVDA or a console narrator to walk. The game's own reader covers them: the review cursor reads every live document, and `folio.Accessibility.Dump` prints them, marked as reachable only that way. A terminal the player walks up to and uses should put its interface on a screen document while in use.

The Widget Reflector shows the proxies **while the tree is built** (`folio.Accessibility.Force 1` first, then take the snapshot), named by what they say. Its picker still lands on the canvas, because the proxies are hit-test invisible, so this is the tree view only. To inspect a document, use `folio.Debugger` or the VSCode extension.

Text scale reaches type sized in `rem` and nothing sized in `dp`. Reduced motion reaches anything a stylesheet moves and nothing a shader, a video or your own per-frame code moves.

Braille is a display plugged into NVDA or JAWS, rendering the same tree it speaks, so on Windows it follows from roles and values being right; Unreal's text pattern support is only partial. On a console there is no braille display support to reach.

Subtitles, remappable controls, difficulty options and anything CVAA touches are game features: Folio can render them but not provide them.

## Where to go next

- **[Testing a document](testing_a_document.md)**, where `--a11y` fits with the rest of the checks.
- **[What Folio does not do yet](known_gaps.md)**: the limits of the screen-reader support.
- **[Themes](../UI_CONVENTIONS.md)** §13: overriding colours without breaking the guarantee above.

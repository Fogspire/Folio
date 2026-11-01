# Screen reader

A Folio menu reads itself out as the player moves through it ("Audio, master volume down, button"), through NVDA, JAWS or a console narrator, or through the game's own voice when none is running.

## Turn it on

**Someone running a screen reader** (NVDA or JAWS on Windows, a narrator on a console) needs nothing switched on. The platform asks for the accessibility tree, Folio builds one, and that software reads it with the player's own settings. This route matters most.

**Someone with no screen reader** gets nothing from that. On a console there is often nothing to run; on Windows many players who need menus read do not run one. `SetScreenReader(true)` builds the same tree and speaks the focused control through the platform's own voice. One back end ships, SAPI, which is part of Windows: no library and no voice data to ship. See [speaking on other platforms](#speaking-on-other-platforms).

Put that setting in your options menu, off by default, saved and applied before the first menu opens. `UFolioAccessibilityLibrary` has it, along with speech rate and volume, and `IsSpeechAvailable` so a platform with no voice hides the row. `folio.Accessibility.Reader 1` turns it on from the console without an options menu.

It says name, then role, then state, then value ("Master volume down, button", "Subtitles, check box, checked"), with the enclosing group named on the way in and not repeated after that: "Audio, master volume down, button".

### Read what it says

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

In a running game, `folio.Accessibility.Dump` prints the same for every screen document, from the live tree when a screen reader is running and from the document otherwise. `folio.Accessibility.Force 1` builds the tree with nothing listening, for measuring it. With `LogFolio` at Verbose, every sentence it speaks is logged.

## If it sounds wrong

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

**The screen behind a dialog is read out.** A dialog's background is out of the tree only when it carries both properties [modals](modals.md) already tells you to write:

```css
.screen.is-behind-modal { focus: none; pointer-events: none; }
```

`folio-inert` says the same thing outright, for a game that traps focus its own way. Either property alone is ordinary (`pointer-events: none` is how a decoration stops eating clicks), so both are required.

**A screen sounds like it is talking about the past.** Lines are queuing faster than they are spoken. `folio.Accessibility.Verbosity` reports how many are waiting; see [what it does when it falls behind](#what-it-does-when-it-falls-behind).

**NVDA says "text" for a heading, "pane" for a region, or "slider" for a progress bar.** Unreal's accessibility layer has no role for a heading or a region, so they arrive as text and as a pane, and a progress bar becomes a slider because that is the only role in the list carrying a value and a range. The game's own reader says the word, and heading navigation works either way.

**A document drawn in the world is silent under NVDA.** `UFolioWorldDocument` and `UFolioMarkerDocument` draw into the scene rather than into a Slate widget, so there is no widget tree for NVDA or a console narrator to walk. The game's own reader covers them: the review cursor reads every live document, and `folio.Accessibility.Dump` prints them, marked as reachable only that way. A terminal the player walks up to and uses should put its interface on a screen document while in use.

**The Widget Reflector shows no proxies.** They exist only **while the tree is built**: run `folio.Accessibility.Force 1` first, then take the snapshot, and they appear named by what they say. Its picker still lands on the canvas, because the proxies are hit-test invisible, so this is the tree view only. To inspect a document, use `folio.Debugger` or the VSCode extension.

## Reading without moving

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

## How much it says

```cpp
UFolioAccessibilityLibrary::SetReaderVerbosity(EFolioReaderVerbosity::Low);
```

| | |
|---|---|
| `Low` | The name, the state and the value. Nothing else. |
| `Normal` | Adds what kind of control it is, and the panel it is in on the way into one. The default. |
| `High` | Adds the `folio-description`. |

Someone who has learned a screen wants "Master volume, 60"; someone meeting it wants to be told it is a slider and what it does.

## What it does when it falls behind

Everything the reader says goes through one queue Folio owns, because a line handed to a voice cannot be taken back, and a player holding a key on a slider produces one line per frame.

- **A focus change is assertive.** It drops the queue and cuts off what is speaking, because everything waiting was about a control the player has already left.
- **An announcement is polite, and carries a channel.** A later line on the same channel *replaces* an earlier one still waiting, so a value that keeps changing leaves one pending line rather than forty, and what gets read is the number it ended on.
- **A polite line that has waited more than four seconds is dropped unsaid.** By then the screen has moved on.
- **A read-all queues and never expires.**

## Typing

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

What else counts as a name is listed under [`W-A11Y-NONAME`](accessibility.md#if-something-is-wrong).

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

## What ends up in the tree

- Controls, always: anything with a control tag, an `onclick`, a `folio-drag`, a `folio-close`, or a `tab-index: auto`.
- Text, wherever an element has words of its own.
- Containers, only when named with `folio-label`. An unnamed panel is layout.
- Nothing that is `folio-hidden`, hidden by `visibility`, collapsed to no size, or faded out, except a control drawn at `opacity: 0` over its own hit area (the styled checkbox), which is still what a click hits.
- Nothing RmlUi generated for itself, such as a scrolling panel's scrollbars.

A caption already spent naming a control is not repeated as text of its own, so a checkbox and its label do not read as "Subtitles, check box, checked. Subtitles."

## What happens

While a platform screen reader is listening or the game's own reader is on, every screen document produces a tree of hit-test-invisible, unfocusable Slate widgets: one per group, control, list, item and run of text. Each carries a name, a role, a value, a checked or disabled state and the bounds of its element. Unreal builds its accessibility tree from those, and the platform reads it.

Moving through a document with the pad or the keyboard tells the platform what was moved onto, so a menu reads itself out as it is navigated. Activating a control from the screen reader focuses, then presses, the element.

Nothing exists while nothing is listening: no widgets, no walk, one branch a frame.

### What it costs

Two costs, separated because a document that changed has usually not changed what it *means*.

**Following the elements** happens on every frame the document changed: one box read per node, no strings, no allocation. A HUD animating a bar pays only this.

**Walking the document** happens at most ten times a second (`folio.Accessibility.WalkInterval`), and immediately when a screen reader starts or focus lands somewhere the tree has not seen, so a dialog that opens is announced the next frame. The walk is proportional to the document; `stat Folio` reports it as *Accessibility walk*, and nothing while nobody is listening.

## The reader's own words

"check box", "not checked", "unavailable", the name of the comma key: all of it is `NSLOCTEXT` under the `FolioAccessibility` namespace in `FolioAccessibilityText.cpp`. Unreal's ordinary source gather finds it, so it lands in **your project's** localisation target rather than the one the markup goes to.

Nothing is assembled by concatenation: "deleted a" is a format string with an argument, because other languages order it differently. Numbers go through `FText::AsNumber`, so a slider reads its value the way the culture writes one.

## Speaking on other platforms

`Folio::FSpeech` takes a back end. SAPI ships because it is part of Windows; every other platform's speech API sits behind an SDK this plugin cannot see, so install one from a module that can:

```cpp
// From your platform module's StartupModule, before the reader is ever turned on.
Folio::FSpeech::SetBackend(MakeUnique<FMyPlatformVoice>());
```

`Folio::ISpeechBackend` is six functions (start, stop, say, silence, rate, volume). Only `Say` has a rule: a queued line waits for whatever is being spoken, an interrupting one drops it. A back end with no queue of its own should speak immediately rather than drop the line.

Rate and volume live above the back end, so a player's choice survives the reader being toggled and a back end being swapped.

The `Folio.Accessibility.Speech` test installs a back end that records what it was told to say, so what a menu reads out is an assertion.

## Braille

Braille is a display plugged into NVDA or JAWS, rendering the same tree it speaks, so on Windows it follows from roles and values being right; Unreal's text pattern support is only partial. On a console there is no braille display support to reach.

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

## Where to go next

- **[Accessibility checks](accessibility.md)**: `--a11y`, which fails the build on a control with no name.
- **[What Folio does not do yet](known_gaps.md)**: the limits of the screen-reader support.

# Accessibility

Three parts. The first is everything you can get right in the markup and the stylesheet: readable
text, enough contrast, hit targets you can actually hit, a focus state you can see, and a name on
every control. `uipreview --a11y` checks all of it and fails the run under `--strict`, so it is a
build gate rather than a thing to remember.

The second is the screen reader. Folio draws through a single Slate widget, and NVDA, JAWS and
console TTS read Unreal's widget tree - so while one of them is running, the document is walked into
a tree of Slate widgets, one per thing worth announcing, and that is what gets read. Nothing is built
while nothing is listening. A player with no screen reader to run gets the same thing spoken by the
game itself.

The third is the settings that change how a screen is presented rather than what it says: larger
text, less movement, higher contrast. Those are three lines of game code and a stylesheet that was
written to expect them.

The first two read the same walk. A control the checker calls named is a control a screen reader
reads a name for, and `--a11y-tree` prints exactly what the engine will build.

## Why a UI toolkit ships this

Four separate pressures, with very different force. They are worth separating, because most of what
gets said about game accessibility conflates a legal requirement with a marketing tag.

**Store metadata, self-declared.** Xbox, PlayStation and Steam all take an accessibility feature
declaration on the product page - screen reader support, subtitle options, remappable controls,
colour-blind modes, no-flashing, and so on. You tick what you support. Nobody audits it up front and
nothing blocks a release, but the declaration is public and wrong entries are found by the people who
depend on them. Nintendo has historically been the least developed of the four here.

**Certification, mandatory but narrow.** Platform cert asks a handful of specific accessibility
questions rather than judging whether a UI is usable. The one with real teeth is the US **CVAA**,
which covers in-game communications - text and voice chat have to be usable by players with
disabilities - and that has been enforceable for years and does appear in cert. Microsoft
additionally publishes the Xbox Accessibility Guidelines and a validation service, which are guidance
rather than pass or fail for most titles.

**Regulation, the direction of travel.** The **European Accessibility Act** came into force in June
2025. Whether game *content* falls in scope is contested; the storefront, the purchase flow and the
account and support services around a game are much clearer targets. There is no settled answer for
games yet, which makes this pressure rather than a checklist - and pressure that only moves one way.

**Reputation.** Specialist outlets and player communities review this directly, and menus that cannot
be read out are the most commonly cited failure of the lot.

Where a UI toolkit sits in that: it can provide the plumbing - a readable tree, contrast and target
gates, a name on every control, text scale, reduced motion, a high-contrast theme - and it can make
the checks a build gate so the annotations exist before there are four hundred of them. It cannot
provide the game half. Subtitles, remapping, difficulty options and anything CVAA touches are game
features that Folio can render and cannot supply.

Two practical consequences for anyone using this:

- The declaration you can honestly make from Folio alone is "screen reader support in menus", "text
  size options", "reduced motion" and "high contrast", plus the visual ones the checker gates.
  Everything else on a store form is yours to build.
- Specifics per platform move constantly. Read the current requirements on each platform's own
  developer portal before filling in a submission form rather than trusting a summary - including
  this one.

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
| `W-A11Y-CLIPPED` | Something is bigger than the box it is in, and the box hides what does not fit. |
| `W-A11Y-CVD` | Under `--simulate`, text that only passes contrast because of a hue difference that vision does not have. |
| `I-A11Y-NOBACKDROP` | Some text has no opaque background above it, so its contrast is unknowable. Informational. |
| `I-A11Y-FIXEDTEXT` | Under `--text-scale`, text the setting could not move. Informational: some text is right not to move. |

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

### Two players, one tree

**Someone running a screen reader** - NVDA or JAWS on Windows, a narrator on a console - needs nothing
switched on. The platform asks the application for its accessibility tree, Folio builds one, and that
software reads it in its own voice with its own settings. This is the route that matters most, because
it is the one those players have already configured to their liking.

**Someone with no screen reader** gets nothing from that. On a console there is often nothing to run;
on Windows plenty of players who need menus read do not run one. For them a game has to read itself,
and Folio will: `SetScreenReader(true)` builds the same tree and speaks the focused control through
the platform's own voice. One back end ships, SAPI, which is part of Windows - no library and no voice
data to ship. See [speaking on other platforms](#speaking-on-other-platforms).

That setting belongs in your options menu, off by default, saved with everything else and applied
before the first menu opens. `UFolioAccessibilityLibrary` has it, along with speech rate and volume,
and `IsSpeechAvailable` so a platform with no voice hides the row rather than offering a dead one.

What it says is name, then role, then state, then value - "Master volume down, button", "Subtitles,
check box, checked" - with the enclosing group named on the way in and not repeated after that:
"Audio, master volume down, button".

### Reading without moving

Focus reaches the controls. It does not reach a heading, a paragraph, a warning, a score, or the label
above the field you are standing in - so a player who can only hear what focus lands on hears a menu
with all of its words removed. Every real screen reader solves this with a second cursor the player
drives independently, and a game that speaks for itself has to provide one.

```cpp
UFolioAccessibilityLibrary::ReviewNext();       // and Previous, First, Last
UFolioAccessibilityLibrary::ReviewRepeat();     // that again
UFolioAccessibilityLibrary::ReviewAll();        // from here to the end of the screen
UFolioAccessibilityLibrary::ReviewStop();       // what any other key should also do
UFolioAccessibilityLibrary::ReviewActivate();   // press what the cursor is on
UFolioAccessibilityLibrary::ReviewMoveFocusHere();
```

Bind next, previous, repeat and read-all to something reachable with one hand; the rest are
conveniences. Each returns what was said, so a game can put the same line on screen.

The cursor covers every live document, newest first, so a modal is read before the screen behind it -
**including a document drawn in the world**, which has no Slate widget and which no platform screen
reader can see. It follows focus, so arrowing through a menu and then reading on works the way round
anyone would expect, and it steps over the layout containers that have nothing to say.

`ReviewNextHeading` and `ReviewPreviousHeading` jump between the things that title a part of the
screen, which is how a long page is read without walking every line of it. They need the document to
use `<h1>`-`<h6>` or `folio-role="heading"` - the same markup that makes it read as structured
anywhere else.

`folio.Accessibility.Review [list|next|previous|first|last|heading|prevheading|repeat|all|stop|activate|focus]`
drives it from the console, which is how to try it before anything is bound.

### How much it says

```cpp
UFolioAccessibilityLibrary::SetReaderVerbosity(EFolioReaderVerbosity::Low);
```

| | |
|---|---|
| `Low` | The name, the state and the value. Nothing else. |
| `Normal` | Adds what kind of control it is, and the panel it is in on the way into one. The default. |
| `High` | Adds the `folio-description`. |

Someone who has learned a screen wants "Master volume, 60"; someone meeting it wants to be told it is
a slider and what it does. Each finds the other setting close to unusable, which is why every real
screen reader has this and why a game that speaks for itself needs it too.

### What it does when it falls behind

Everything the reader says goes through one queue that Folio owns, rather than into the voice as it
happens. That is the whole point: a line handed to a voice cannot be taken back, and a player holding
a key on a slider produces one line per frame.

- **A focus change is assertive.** It drops the queue and cuts off what is speaking, because
  everything waiting was about a control the player has already left.
- **An announcement is polite, and carries a channel.** A later line on the same channel *replaces* an
  earlier one still waiting, so a value that keeps changing leaves one pending line rather than forty,
  and what gets read is the number it ended on.
- **A polite line that has waited more than four seconds is dropped unsaid.** By then the screen has
  moved on and reading it would describe a state that no longer exists.
- **A read-all queues and never expires.** A player asked for that, so it waits as long as it takes.

`folio.Accessibility.Verbosity` reports how many lines are waiting, which is the number to watch if a
screen ever feels like it is talking about the past.

### Typing

A text field's contents are one accessible value, and a value changing is not an event a screen reader
announces - so a player typing their name into an empty box would otherwise hear nothing at all from
the first keystroke to the last. The game's own reader echoes as you type:

- each character as it goes in, with punctuation named, because a voice handed a bare full stop says
  nothing and the player has then been told their keystroke did nothing
- a pasted or otherwise multi-character run as words rather than one symbol at a time
- what was deleted, as it goes
- "blank" when the field empties, which is the one state a player cannot infer
- the selection when it changes, and the text the caret moves over

A password field says stars, never characters. Reading a password out loud is the one place where
doing the accessible thing is the wrong thing.

Only the game's own reader does this. A platform reader has its own character echo, and doing it here
as well would say every letter twice.

### The reader's own words

"check box", "not checked", "unavailable", the name of the comma key - a screen reader is about a
third its own vocabulary, and all of it is `NSLOCTEXT` under the `FolioAccessibility` namespace in
`FolioAccessibilityText.cpp`. Unreal's ordinary source gather finds it with no extra step, so it lands
in **your project's** localisation target rather than the one the markup goes to. That is where a
string with no document belongs.

Nothing is assembled by concatenation: "deleted a" is a format string with an argument, because it is
the same sentence in a different order in other languages. Numbers go through `FText::AsNumber`, so a
slider reads its value the way the culture writes one.

### Speaking on other platforms

`Folio::FSpeech` takes a back end. SAPI ships because it is part of Windows; every other platform's
speech API sits behind an SDK this plugin cannot see, so those arrive from a module that can:

```cpp
// From your platform module's StartupModule, before the reader is ever turned on.
Folio::FSpeech::SetBackend(MakeUnique<FMyPlatformVoice>());
```

`Folio::ISpeechBackend` is six functions - start, stop, say, silence, rate, volume - and the only one
with a rule attached is `Say`: a queued line waits for whatever is being spoken, an interrupting one
drops it. A back end with no queue of its own should speak immediately rather than drop the line; a
reader that loses sentences is worse than one that talks over itself.

Rate and volume live above the back end, so a player's choice survives the reader being turned off and
on and survives a back end being swapped in.

It is also what makes the reader testable: `Folio.Accessibility.Speech` installs a back end that
writes down what it was told to say, and then what a menu reads out is an assertion rather than
something a person has to sit and listen to.

### What happens

While either of those is true, every screen document walks itself once per frame *in which it
changed* and produces a tree of hit-test-invisible, unfocusable Slate widgets: one per group,
control, list, item and run of text. Each carries a name, a role, a value, a checked or
disabled state and the bounds of the element it stands for. Unreal builds its own accessibility tree
from those the way it does for any UI, and the platform reads it.

Moving through a document with the pad or the keyboard tells the platform what was moved onto, so a
menu reads itself out as it is navigated. Activating a control from the screen reader presses the
element, focus first, so anything a document does on focus happens for that player too.

Nothing exists while nothing is listening: no widgets, no walk, one branch a frame.

### What it costs

Two costs, deliberately separated, because a document that changed has usually not changed what it
*means*.

**Following the elements** happens on every frame the document changed: one box read per node, no
strings, no allocation. A HUD animating a bar pays this and nothing else.

**Walking the document** happens at most ten times a second - `folio.Accessibility.WalkInterval` - and
immediately when a screen reader starts or when focus lands somewhere the tree has never heard of, so
a dialog that opens is announced the next frame rather than a tenth of a second later. That walk is
proportional to the document; `stat Folio` reports it as *Accessibility walk*, and it reports nothing
at all while nobody is listening.

### Modals

A dialog's background is out of the tree. The signal is the pair of properties
[docs/modals.md](modals.md) already tells you to write:

```css
.screen.is-behind-modal { focus: none; pointer-events: none; }
```

`folio-inert` says the same thing outright, for a game that traps focus its own way. Either alone is
ordinary - `pointer-events: none` is how a decoration stops eating clicks - so both are required.

A screen reader that can still walk into a dialog's background is the same bug as a gamepad that can:
the player is told about controls they cannot reach, with no way to know why nothing happens.

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
`folio.Accessibility.Force 1` builds the tree with nothing listening, which is how you measure it, and
`folio.Accessibility.Reader 1` turns the game's own reader on without an options menu to do it from.
With `LogFolio` at Verbose, every sentence it speaks is also logged, so what a player hears can be
read back off a log.

### Changing what a screen says

A tree that is correct is not the same as a tree that reads well, and the difference is only visible
once you read the transcript. Every line you disagree with is one attribute on one element:

| What is wrong with the line | What fixes it |
|---|---|
| The words are wrong, or it is a symbol | `folio-label="..."` |
| It needs a consequence said after the name | `folio-description="..."` |
| It is read as the wrong kind of thing | `folio-role="..."` |
| It should not be read at all | `folio-hidden` |
| It changes and nobody would ever land on it | `folio-announce` |
| It is a switch that reads as its own state | `folio-role="checkbox"` and `folio-checked` |

The practical way to do a whole game rather than one screen: dump the tree for every document into one
file, edit the lines you want changed in place, and work back from that to the markup.

```bash
for doc in UI/screens/*.rml; do
    echo "== $doc"
    ./Plugins/Folio/bin/win64/uipreview.exe "$doc" --root . --a11y-tree
done > accessibility-transcript.txt
```

Reading it out loud is the point of it. A line that is accurate and clumsy - "ON, button", "minus,
button", the same panel name before every row - is a line a player hears a hundred times.

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
| `listitem`, `option` | an item in one |

`<h1>` to `<h6>` are headings without being told, which is worth using: it is the same markup that
makes a document read as structured anywhere else, and it is what the review cursor jumps between.

Heading and region do not survive to a *platform* screen reader. Unreal's accessibility layer has no
role for either, so they arrive as text and as a pane - the closest honest thing in a list that cannot
be extended from a plugin. The game's own reader says the word, and heading navigation works either
way. A progress bar becomes a slider for the same kind of reason: it is the only thing in that list
carrying a value and a range, and "slider, 40" is better than silence.

### `folio-checked`

The state of a check box or a radio built out of `div`s, where there is no input to carry it and a
class name is the only thing that has it.

```html
<span class="switch focusable" folio-role="checkbox" folio-label="Invert look"
      data-attr-folio-checked="bInvertY" data-class-yes="bInvertY">{{ bInvertY ? 'ON' : 'OFF' }}</span>
```

Anything but `false` or `0` is on. A real `<input type="checkbox">` needs none of this - it carries
its own state and Folio reads it.

Worth knowing why the example is written that way at all: `ON` and `OFF` are a *state*, not a name, so
a switch whose visible text is its state needs a `folio-label` and a role. Without them a screen
reader says "ON, button", which tells a player what it currently is and nothing about what it does.

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

Each announcing element has its own place in the queue, so a value that changes forty times leaves one
line waiting and reads the number it ended on. `folio-announce="assertive"` cuts in over whatever is
being said instead - for a warning, never for a score.

```html
<div class="alert" folio-announce="assertive" data-attr-value="model.alert">{{ model.alert }}</div>
```

It is also the answer for a value nobody can navigate to. A stepper row - a label, a minus, a number,
a plus - has three focusable things and the number is not one of them, so a player pressing plus would
never hear what it became. `folio-announce` on the value says it each time it changes.

Game code with no element to hang it on calls `UFolioAccessibilityLibrary::Announce`, which is the
same path. `folio.Accessibility.Say <text>` is that from the console.

## Presentation settings

Three settings that change how a screen looks and nothing about what it means. All three are global,
all three reach documents opened afterwards, and none is on by default or inferred from the operating
system - a player asked for this or they did not.

```cpp
UFolioAccessibilityLibrary::SetTextScale(1.5f);      // 0.5 to 3, 1 being as authored
UFolioAccessibilityLibrary::SetReducedMotion(true);
UFolioAccessibilityLibrary::SetHighContrast(true);
```

`folio.Accessibility.TextScale`, `.ReducedMotion` and `.HighContrast` do the same from the console.

### Text scale

Applied by setting the root font size, so it reaches every rule written in `rem` and no rule written
in `dp`. That is a real constraint rather than an implementation detail, and it is why the shipped
theme sizes all of its type in `rem` against one `dp` root:

```css
body       { font-size: 20dp; }   /* the root, and the only dp size in the theme */
.t-title   { font-size: 1.4rem; }
.t-body    { font-size: 1rem; }
```

Size your own type the same way if you want the setting to reach it. Preview the result, and find out
what it could not move:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --text-scale 1.5
```

Two findings come out of that run. `I-A11Y-FIXEDTEXT` names each piece of text the scale did not move,
measured rather than inferred from the unit - it is informational because some text is right not to
move: a letter drawn inside a key cap, a logo, a version string. `W-A11Y-CLIPPED` names anything that
stopped fitting, which is the failure that actually ships: a fixed-height row whose label now overlaps
the one under it. That one is a warning and fails `--strict`.

Every shipped recipe is checked at 150% as well as at 100%.

### Reduced motion

Not a taste setting. Vestibular disorders make large movement genuinely unpleasant, and a menu that
slides, scales and parallaxes is the usual trigger.

It is a theme, `reduced-motion`, declared in `rcss/folio.rcss` - the user-agent stylesheet every
document links - so it reaches a game's own rules and elements the theme has never heard of:

```css
@media (theme: reduced-motion)
{
    * { transition: none; animation: none; }
}
```

What a stylesheet cannot reach is movement written from code every frame. `UFolioHudMotion` reads the
setting and snaps to rest; anything else you drive yourself should read
`UFolioAccessibilityLibrary::IsReducedMotionOn` and do the same. A shader that moves, a video, and a
`<text>` that types itself out are all outside it.

`uipreview --reduced-motion` turns on the same theme, for judging a screen with everything still.

### High contrast

`rcss/themes/default.rcss` carries a `high-contrast` block, and it is deliberately not just brighter:
the focus ring gains width as well as contrast, because the players who need this are often the ones a
2dp ring is invisible to, and disabled controls stop relying on opacity alone.

A screen that writes its own colours has to answer the setting itself, because a theme cannot reach a
rule inside a document's own `<style>`:

```css
@media (theme: high-contrast)
{
    body { background-color: #000000; }
    h1   { color: #c6c6d2; }
}
```

Check one at the ratio a high-contrast theme should meet rather than the ordinary one:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --theme high-contrast --a11y-contrast 7
```

### Colour vision

Around one man in twelve has some form of colour vision deficiency, which makes it by far the most
common visual difference a UI meets - and the one nobody catches by looking, because the person who
drew the screen sees it working.

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --a11y --screenshot out.png \
    --simulate deuteranopia
```

Every capture is redrawn as that vision sees it, and the contrast pass runs again on the simulated
colours. `W-A11Y-CVD` reports the pairs that pass as drawn and fail as seen - text that only clears
the threshold because of a hue difference the player does not have. `protanopia`, `deuteranopia`,
`tritanopia` and `achromatopsia` are the four, at full severity, using the Machado 2009 transforms.

What no tool can check for you is meaning carried by colour alone: a red bar and a green bar that are
the same bar to a third of the players who see them. Add a shape, a label or a position.

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

A document in the world is not exposed to a *platform* screen reader. `UFolioWorldDocument` and
`UFolioMarkerDocument` draw into the scene rather than into a Slate widget, so there is no widget tree
for NVDA or a console narrator to walk and no useful screen position to report if there were. The
game's own reader does cover them: the review cursor reads every live document, and
`folio.Accessibility.Dump` prints them, marked as reachable only that way. A nameplate over a
character is not a control; a terminal the player walks up to and uses is, and the answer for one of
those is still to put its interface on a screen document while it is being used.

Text scale reaches type sized in `rem` and nothing sized in `dp`. Reduced motion reaches anything a
stylesheet moves and nothing a shader, a video or your own per-frame code moves.

Braille is a display plugged into NVDA or JAWS, rendering the same tree it speaks - so on Windows it
follows from roles and values being right, and Unreal's text pattern support is only partial. On a
console there is no braille display support to reach.

An accessibility declaration for a storefront is also more than a readable tree. Subtitles, remappable
controls, difficulty options and anything CVAA touches are game features, not UI-toolkit features -
Folio can render them but cannot provide them.

## Where to go next

- **[Testing a document](testing_a_document.md)** — where `--a11y` fits with the rest of the checks.
- **[What Folio does not do yet](known_gaps.md)** — the limits of the screen-reader support.
- **[Themes](../UI_CONVENTIONS.md)** §13 — overriding colours without breaking the guarantee above.

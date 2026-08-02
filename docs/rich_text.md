# Rich text, terms and the typewriter

Dialogue is the case ordinary markup handles badly. A line has emphasis in the middle of it, names
that have to read the same everywhere, pacing that belongs to how it is said, and it has to be
translatable — as **one string**, because a translator has to be able to move the emphasis onto
whichever word carries it in their language.

`<text>` is that string.

```html
<text class="line" data-loc="line_1">You <angry>must</> go to the {tag:Place.Mountain.Temple},
	I{delay:0.2}...{/} think?</text>
```

```css
.line  { reveal: typewriter; reveal-rate: 45; reveal-punctuation: 0.18; }
.angry { color: #e06c75; font-weight: bold; }
.term  { color: #61afef; }
```

"must" is bold and red. "Mountain Temple" comes out of a table shared by the whole game and carries
that table's colour. The ellipsis types slowly and the words after it go back to normal speed. The
whole thing is one translatable string with one key.

**What is inside a `<text>` is not RML.** It is read raw and parsed by Folio, so `<angry>` is a
style rather than an element and `{tag:}` is a term rather than a data binding. That is exactly what
makes it translatable: a translation is a string, and a string cannot introduce elements into a
document. Anything a translator sends back is markup Folio reads, never markup RmlUi parses.

## The grammar

Small, and it closes the same way twice.

| | |
|---|---|
| `<style>words</>` | Applies the class `style` to what it encloses. `</style>` also works where naming it reads better. |
| `{tag:Some.Thing}` | Substitutes a name from `UI/terms`. Does not enclose anything. |
| `{delay:0.2}slow{/}` | Every character inside waits 0.2s. The one after it goes back to the rate. |
| `{pause:0.5}` | Waits once, before the next character. |
| `{br}` | A line break. |
| `\<` `\>` `\{` `\}` `\\` | The literal character. |

Styles nest, and the classes accumulate: `<angry><whisper>x</></>` puts `class="angry whisper"` on
the run. `</>` and `{/}` each close the nearest of their own kind.

**A style tag becomes a class of the same name.** There is no fixed vocabulary and Folio defines
none — `<angry>` means whatever `.angry` says it means. The default theme ships `.angry`, `.shout`,
`.soft`, `.aside` and `.term` as a starting point; rename them, add to them, delete what your game
does not use. The one rule is that a tag your writers use needs a rule somewhere, or the word it
marks renders as ordinary text and nobody finds out.

Whitespace collapses, so a long line can be indented in the document the way the rest of it is.

## Terms

A term is a name written once: a place, a character, an item, a quest. Every document and every
translation refers to it by tag, so the name is translated once and every line agrees about it.

```
# UI/terms/places.terms
@class term

Place.Mountain.Temple = Mountain Temple
Place.Village.Kurogane = Kurogane Village
```

One file per group of names, all loaded at startup, all merged. `@class` names the style class every
entry in that file carries, which is how a place reads differently from the sentence around it
without anything being said at each use.

Tags are strings that look like gameplay tags, and nothing checks them against a gameplay tag
registry — the table is UI data. A term defined twice is an error naming both files, because a name
meaning two things is the failure the whole mechanism exists to prevent.

`Tag:form = ...` adds a named form of an entry, reached as `{tag:Place.Mountain.Temple:gen}`.
English uses none. It exists for languages that inflect a name, where a translator otherwise cannot
build a sentence around a fixed noun.

**These are files rather than engine data on purpose.** The preview tool has no engine, and a term
that only resolved in game would make every previewed screen a lie.

- `folio.Terms` prints every loaded term and what it resolves to.
- `folio.Terms.Reload` reads the tables again and rebuilds every open document. Saving a `.terms`
  file does the same thing on its own with hot reload on.

## The typewriter

`reveal: typewriter` types the line out one character at a time, each fading in.

All of it is RCSS, and all of it is inherited, so a game decides how its dialogue reads once:

| | | |
|---|---|---|
| `reveal` | `none` \| `typewriter` | Off by default; a `<text>` is then simply styled text. |
| `reveal-rate` | number | Characters per second. 45 is conversational, 90 is brisk. |
| `reveal-fade` | number | Seconds each character takes to fade in. |
| `reveal-rise` | length | How far each character lifts as it appears. `0` for none. |
| `reveal-punctuation` | number | Extra seconds after `.` `!` `?`, half that after `,` `;` `:`. |
| `reveal-sound` | string | Named and reported, never played. See below. |
| `reveal-start` | `auto` \| `manual` | `manual` waits for the game to call `Play()`. |

`reveal-punctuation` is the one worth playing with. An even stream of characters reads flat however
well the line is written, and a beat after a full stop is most of what makes it sound like someone
talking. `{delay:}` and `{pause:}` are for the moments the writer wants specifically.

### The element carries its own state

```css
.line.is-revealing ~ .continue { opacity: 0; }
.line.is-revealed  ~ .continue { opacity: 1; }
```

A continue prompt that appears when the line has finished is a stylesheet rule rather than an event
listener. `revealcomplete` is dispatched as well, for anything that needs to run code.

### Driving it from the game

A line the game owns - dialogue from a script asset, a quest description, a log entry - does not
live in the document at all:

```cpp
Document->SetElementMarkup(TEXT("line"), Line.ToString());
```

That replaces the `<text>`'s markup and starts it revealing. The string is game text, translated
through whatever asset it came from, so the element carries `data-noloc` rather than a key.

`OnTextRevealed` reports the id of a line that has finished, whether it ran to the end or was
skipped, which is what a conversation waits on before offering the next one. `SkipElementReveal`,
`IsElementRevealing` and `GetElementRevealDuration` are there for the input and the auto-advance.

Author the document with a line in it anyway, marked `data-noloc`: it costs nothing, the game
overwrites it, and it is the difference between a screen you can preview and one you cannot.

### Skipping

A player who does not want to wait presses something, and the line completes at once:

```cpp
if (Folio::Text::FElementRichText* Text = Folio::Text::AsRichText(
		Canvas->GetDocument()->GetElementById("line")))
{
	Text->Skip();
}
```

`Play()` restarts it, `IsRevealing()` and `IsComplete()` say where it is, and `GetDuration()` is how
long it will take from the start — which is what an auto-advance timer wants.

`folio.Text.Skip` does the same thing to every revealing line, for testing pacing without a
controller in your hand.

### Sound

`reveal-sound: "dialogue-blip"` names a sound and plays nothing, in keeping with the rest of
[UI sounds](ui_sounds.md). The game hears about it:

```cpp
void UMyAudioSubsystem::Initialize(FSubsystemCollectionBase&)
{
	FFolioModule::Get().OnTextRevealSound().AddUObject(this, &UMyAudioSubsystem::PlayNamedSound);
}
```

At most once a frame, however fast the text is typing: a click per character at sixty characters a
second is a buzz rather than a sound.

## How it is drawn, and why that matters

The runs become real elements — `<angry>x</>` is a `<span class="angry">` — so wrapping, alignment,
line height, nesting and theming are RmlUi's, exactly as for any other text. The contrast check sees
them too.

While a line is revealing, those spans are `visibility: hidden` and Folio draws the glyphs itself,
reading back the lines RmlUi laid out and painting each glyph with its own alpha. Three consequences
worth knowing:

- **The text never moves.** Every character is laid out from the first frame, so the line wraps
  once, before anything is visible. Anything that revealed by growing the string rewraps on almost
  every character.
- **Shaping is intact.** Each run is shaped whole, so ligatures, kerning and mark attachment are
  what they would be without a reveal.
- **It costs a mesh per frame while it types, and nothing once it stops.** On the last character the
  spans are shown again and RmlUi draws them from geometry it generates once.

A `<text>` with `reveal: none` never takes that path at all.

## Translating it

The markup is part of the string, and a translator sees it. That is a deliberate choice - it is what
Unreal's own rich text does, and every alternative still makes them place the markers while telling
them less. Three things make it safe:

1. **`gather_rml.py` writes a translator brief** beside the generated header: what the tags mean,
   what each `{tag:}` prints in English, and which strings carry which. Unreal's PO export has no
   room for a note against a string, so it travels beside the PO.
2. **The gather fails on bad markup.** An unclosed tag, an unknown directive or a term with no entry
   is an error with a file and a line, before anything ships.
3. **What comes back is checked**:

   ```bash
   ./scripts/loc.sh --check-po Folio.po
   ```

   Every translation is parsed. A retyped tag, an invented style or an unclosed one is an error; a
   translator dropping a term is a warning, because writing a name out in full is a legitimate call
   when grammar demands it.

Terms are gathered into their own namespace, `Folio.Terms`, keyed by tag. A name is translated once
there and every line that mentions it follows.

`--culture pseudo` inflates and accents the words and leaves the tags exactly as they were.
Accenting a term tag would only prove that a corrupted tag does not resolve, which is not the
question pseudo-localisation asks. See **[Localisation](localization.md)**.

## Limits

- **One `data-loc` covers the whole line**, which is the point, but it also means a `<text>` cannot
  contain an `<img>`, a binding or any other element. It is prose.
- **`{{ }}` data expressions do not work inside it.** The braces belong to the markup. A line that
  has to name the player is a job for a term or for `SetMarkup` from game code.
- **`text-transform` and the reveal disagree.** The reveal walks the laid-out characters against the
  ones it parsed; a transform that changes their length makes it fall back to revealing in order,
  which is usually still right and is not guaranteed to be.
- **Right-to-left text reveals left to right.** Bidi is a
  [known gap](known_gaps.md) in the text engine generally.

## See also

- **[Localisation](localization.md)** — the gather, the namespaces and the whole pipeline.
- **[Text and fonts](text_and_fonts.md)** — faces, weights and font effects, which apply here as
  they do anywhere.
- **[`rcss/recipes/dialogue.rml`](../rcss/recipes/dialogue.rml)** — a working dialogue box to copy.

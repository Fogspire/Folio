# A dialogue system

A speaker, a line that types itself out, and replies. The line is game data and carries a writer's
markup; the names in it come from a table the whole game shares.

Working version: [`rcss/recipes/dialogue.rml`](../../rcss/recipes/dialogue.rml).

```bash
./bin/win64/uipreview.exe rcss/recipes/dialogue.rml --root . --size 1280x720
```

---

## 1. The split

Two kinds of thing reach the box, and they arrive by different routes:

| | |
|---|---|
| The **line** | `<text>` + `SetElementMarkup`. It carries markup, and a binding renders a string. |
| Everything else | A view model: speaker, replies, whether the box is up. |

That is the whole design. Getting it backwards is the one mistake worth avoiding: `{{ Line }}` in a
binding prints the braces.

## 2. The document

```html
<text id="line" class="line" data-noloc>Placeholder, for the preview.</text>
```

```css
.line { reveal: typewriter; reveal-rate: 50; reveal-punctuation: 0.16; }
```

`data-noloc` because the line is game text, translated with the asset it came from. Author a
placeholder anyway - the game overwrites it, and it is the difference between a screen you can
preview and one you cannot.

Replies repeat, and carry an id so focus can name one:

```html
<div class="reply focusable" data-for="reply : Replies" data-attr-id="reply.Id"
	data-event-click="Select(reply.Index)">{{ reply.Text }}</div>
```

A continue prompt needs no code - `<text>` carries its own state:

```css
.line.is-revealed ~ .foot .continue { opacity: 1; }
```

## 3. The terms

One file per group of names. Markup reaches them as `{tag:Place.MountainTemple}`.

```
# UI/terms/places.terms
@class term

Place.MountainTemple = Mountain Temple
```

A name is written once, translated once, and every line agrees about it. `@class` is what makes a
place read as a place without anything being said at each use.

**A game that already holds its names** - a data asset, a table of `FText` - mirrors them into the
file and marks it `@external`. Nothing gathers those strings, and the game pushes the real text in:

```cpp
Folio::SetTermText(TEXT("Place.MountainTemple"), Name);
```

Do that on `Folio::OnTermsLoaded()`, not once at startup: a reload re-reads the files and drops
whatever was pushed.

## 4. The widget

Write each line as it arrives, and report the reveal:

```cpp
Document->SetElementMarkup(TEXT("line"), Line.ToString());
```

```cpp
Document->OnTextRevealed.AddDynamic(this, &UMyDialogue::HandleLineRevealed);
```

`SkipElementReveal` is the skip button. `OnTextRevealed` fires for a skipped line too, so a
conversation that waits on it has one path out rather than two.

**A line written before the document exists is dropped.** The widget is usually pushed and asked for
its first line in the same breath, and the canvas has not built one yet. `SetElementMarkup` returns
false; hold the line and try again next tick rather than treating it as shown.

## 5. What it costs

A revealing line rebuilds its glyph mesh every frame and stops the moment it lands. Fine for a box,
not for a screen of them.

## See also

- **[Rich text, terms and the typewriter](../rich_text.md)** - the grammar, every property, the
  limits.
- **[Localisation](../localization.md)** - the gather, the translator brief, `--check-po`.

# Input prompts

"Hold **E** to file a complaint." The part that is hard is not the sentence: it is that **E** has to be whatever the player actually bound, has to become a face button the moment they pick up a pad, and has to look like something without waiting on an artist.

## What a prompt is made of

```cpp
Prompts = NewObject<UFolioPromptSet>(this);
Prompts->Add(TEXT("Interact"), TEXT("File a complaint"));
Prompts->Add(TEXT("Skip"), TEXT("Skip"));
Prompts->Refresh(GetOwningLocalPlayer());

Document->BindViewModel(TEXT("prompts"), Prompts);
```

```html
<span class="prompt" data-for="p : Prompts" data-if="p.bBound">
    <span class="pg pg-key">{{ p.Glyph.Cap }}</span>
    <span class="pg-label">{{ p.Label }}</span>
</span>
```

Game code names the actions once. `Refresh` resolves each to the key the player has bound **and filters by what they are holding** - a pad in hand never shows `F`. Call it when the screen opens, when the device changes, and after a rebind; it is cheap.

`bBound` is false when nothing is bound. Hide the row: a prompt for an unbound action sends the player looking for a key that does nothing.

## Where the picture comes from

Three sources, and a project picks the order per device family:

```cpp
// Keyboard drawn, gamepad from licensed art with a drawn fallback.
Folio::SetGlyphSources(
    { EFolioGlyphSource::Drawn },
    { EFolioGlyphSource::Image, EFolioGlyphSource::Drawn });
```

| | |
|---|---|
| `Drawn` | The stylesheet's own shapes. No art, no atlas, no licence, sharp at any size, recolours with the theme. |
| `Sprite` | A sprite named by the project's `FFolioKeyGlyph` table. |
| `Image` | A texture. This is how CommonInput's controller art arrives: the brush is resolved, its texture registered, and the prompt gets a `folio://` source. |

**The default is drawn everywhere**, which is what makes prompts work in a project with no art and no CommonInput data at all. `ResolveGlyph` always fills in `Cap` whichever source wins, so a missing texture degrades to a keycap rather than to a blank.

Two lists rather than one because the answer is usually different. A keyboard prompt drawn as a keycap looks better than any screenshot of a keyboard; a PlayStation face button has art the project has licensed and should be using, and drawing your own is a trademark question as much as a taste one.

`Kind` says which won - `drawn`, `sprite` or `image` - so markup picks a branch instead of testing three fields for emptiness:

```html
<span class="pg pg-key"   data-if="p.Glyph.Kind == 'drawn'">{{ p.Glyph.Cap }}</span>
<span class="pg pg-sprite" data-if="p.Glyph.Kind == 'sprite'"><img data-attr-sprite="p.Glyph.Sprite"/></span>
<span class="pg pg-image"  data-if="p.Glyph.Kind == 'image'"><img data-attr-src="p.Glyph.Image"/></span>
```

## The drawn set

`/folio/lib/prompt.rcss` draws keycaps, mouse buttons, gamepad faces, bumpers, triggers, sticks and the d-pad from boxes, borders and radii. `rcss/recipes/prompts.rml` shows every one of them.

Worth knowing if you restyle them:

- **A keycap's minimum width is what holds it open.** An automatic width comes out as one side's padding plus the text, so a cap sized purely by its label runs its last letter under the border.
- **PlayStation's marks are drawn** - cross from two bars, circle a ring, square an outline, triangle three bars each turned into one side. A letter would be wrong on that platform.
- **A face pad's children position against its padding box**, so a 30dp pad with a 2dp border centres on 13, not 15. Getting this wrong pushes every mark down and right by the border width.
- **Corners beat joins.** A chevron drawn as two bars leaves a wedge where their outer corners nearly meet; drawn as a square with two borders it cannot fail to meet itself.

## Naming keys

`Folio::LabelFor` gives the keycap's text: the project's `FFolioKeyGlyph` row if there is one, else the key's own display name. A table is worth it for the long ones - `Left Shift` is a wide cap and `LSHIFT` is not.

```cpp
Folio::RegisterGlyphTable(MyGlyphTable);
```

Repeatable, and later rows win, so a platform can override the gamepad rows without restating the keyboard ones.

## Where to go next

- **[A controls screen](tutorials/keybinding.md)**: letting the player change what these show.
- **[Images and textures](images.md)**: sprites, and what `folio://` is.
- **[Input and navigation](input_and_navigation.md)**: the device classes markup can style against.

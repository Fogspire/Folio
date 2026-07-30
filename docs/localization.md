# Localization

Folio's text goes through Unreal's own localization pipeline — the same commandlets, the same archive
format, the same PO export, the same Translation Editor — as **its own localization target**. What a
translator receives is one `Folio.po` holding your screens and nothing else, rather than a PO with
every C++ string and asset in the project mixed into it. You can hand someone the UI without handing
them the game.

There is **one extra step**, and it is the only thing about this that is Folio-specific: a script has
to run before Unreal gathers. `GatherTextFromSource` scans C++ for `NSLOCTEXT` and cannot read markup,
so the script reads your documents and writes the declarations it would otherwise never find. Skip it
and the gather succeeds with your UI text quietly missing.

## 1. Key every user-visible string

Put a `data-loc` key on the element, and leave the English text inline:

```html
<h1 data-loc="title">Settings</h1>
<button data-loc="apply">Apply</button>
<p data-loc="raytracing_hint">Not supported on this device</p>
```

The source text stays in the document, so it is readable and previewable. The key is what makes it
gatherable, and what a translation is stored against.

**Keys only need to be unique within their file.** The namespace comes from the path, mechanically, so
it is never something you decide:

| Document | Namespace |
|---|---|
| `ui/screens/settings.rml` | `Folio.Settings` |
| `ui/screens/main_menu.rml` | `Folio.MainMenu` |
| anything under `ui/lib/` | `Folio.Common` |

Shared components all land in `Folio.Common` on purpose: a button's label belongs to every screen that
uses it, not to one of them.

### The key goes on an element holding nothing but text

Translating replaces the element's entire contents. So this does not work:

```html
<!-- Wrong: the span is swallowed into the string and comes back as visible markup -->
<p data-loc="hint">Press <span class="key">E</span> to open</p>
```

Split it, and key the parts:

```html
<p><span data-loc="hint_before">Press</span> <span class="key">E</span>
   <span data-loc="hint_after">to open</span></p>
```

Awkward, and unavoidable: a translator needs to be able to reorder those words, which means they have
to be separate strings whatever system you use.

### Values from your game are not translatable strings

A number, a player name, a percentage — those arrive through a data binding and are game state. Do not
key them:

```html
<span>{{ Health }}</span>              <!-- no data-loc, and none wanted -->
<span data-loc="health_label">Health</span>   <!-- the label is a string -->
```

For text that is genuinely not translatable — a version number, a debug readout — mark it `data-noloc`
so the checks stop asking about it.

## 2. Set the project up, once

Copy Folio's template into your project:

```
Plugins/Folio/templates/Config/Localization/Folio.ini  ->  Config/Localization/Folio.ini
```

Then three lines in `Config/DefaultGame.ini`. The first is the one that matters and the one that is
easy to miss — **without it the gather is correct and every string still renders in English**, because
nothing has told the runtime the target exists:

```ini
[Internationalization]
+LocalizationPaths=%GAMEDIR%Content/Localization/Folio

[/Script/UnrealEd.ProjectPackagingSettings]
+CulturesToStage=en
+CulturesToStage=fr
```

If your project already has its own localization target, exclude the generated header from it, or the
same strings are gathered into two manifests:

```ini
; in Config/Localization/<YourTarget>.ini
ExcludePathFilters=Source/Generated/*
```

**Do not use the Localization Dashboard to change gather settings.** It keeps its own copy of those
settings and regenerates `Config/Localization/Folio.ini` from them, dropping anything it does not know
about — including the directory holding the generated declarations, which silently removes every
string that came from markup. Use the Dashboard for the Translation Editor and nothing else. The
template says so at the top of the file, for whoever opens it next.

## 3. Gather

From your project directory:

```bash
./Plugins/Folio/scripts/loc.sh
```

Bash rather than PowerShell so it runs on Linux as well as in git bash on Windows. Set `UE_ROOT` if
engine discovery fails.

Two things happen, in this order:

1. `gather_rml.py` walks your `.rml` files and writes `Source/Generated/FolioText.h` — one
   `NSLOCTEXT` per key. **Nothing includes that header.** It exists to be read by the gatherer, not
   compiled. Commit it, so a machine that has not run the script can still gather.
2. Unreal's `GatherText` runs, and picks that header up as ordinary source.

The script fails, and stops the gather, on anything that would produce untranslatable or ambiguous
text: a duplicate key meaning two different things, a key with no text under it, a key on an element
containing other elements, a key on a bound value, or two documents deriving the same namespace.

Re-run it whenever you add or change user-visible text.

`--script-only` regenerates the header alone, which is the fast check while writing markup.
`--gather-only` skips it.

## 4. Translate

Nothing here is Folio's. Your strings are in `Content/Localization/Folio/<culture>/Folio.archive`,
exported as `Folio.po` for whoever does the translating, and imported back by the same gather command.

Because the UI is its own target, `Folio.po` is a self-contained handoff: send it to a translator or a
translation service and nothing else about the project goes with it.

The Translation Editor in the Localization Dashboard works normally.

## 5. Check it

```
folio.Loc.Report ui/screens/settings.rml
```

Every key in the document, with what it resolved to:

```
localisation of 'ui/screens/settings.rml' in namespace 'Folio.Settings', culture 'fr':
  title                    [loc]    Paramètres
  apply                    [loc]    Appliquer
  raytracing_hint          [source] Not supported on this device
  35 keyed string(s), 34 with a translation
```

`[loc]` found a translation. `[source]` did not, and is showing English. This is the only way to tell a
**missing translation** from a **missing key**, which look identical on screen — a key with no
translation reports `[source]`, while text with no key does not appear in the list at all.

To see a whole screen's worth at once before any translation exists, the preview tool inflates and
accents every string:

```powershell
uipreview.exe ui\screens\settings.rml --root . --culture pseudo --strict
```

Pseudo-localization is how you find the layout that only fits English. It lengthens each string by
about 40%, accents the letters and wraps it in brackets — **if the closing bracket is missing, that
string is being clipped.** Values arriving through a binding are left alone, so a number beside an
accented label is the binding working rather than a string that was missed.

`--strict` turns every warning into a failure, including text with no key at all. That is the check
worth running before committing a document.

## If something looks wrong

- **Every string reports `[source]` and the archive clearly has translations.** The target is not
  loaded. Check `+LocalizationPaths=%GAMEDIR%Content/Localization/Folio` is in `DefaultGame.ini`. This
  is the most common setup mistake, because everything else looks right.
- **Gather succeeded but the UI text is not in the manifest.** The script did not run. Use `loc.sh`
  rather than the Dashboard's Gather button, which cannot run it.
- **Every string reports `[source]` in a culture that has translations.** The namespace is not
  matching. Check the document's path against the table above; a file that moved changes its
  namespace, and the old translations stay under the old one.
- **One string reports `[source]`.** Ordinary missing translation. It will appear in the PO export as
  untranslated.
- **A translated string shows visible `<span>` markup.** The key is on an element containing other
  elements. See above.
- **A string does not appear in `folio.Loc.Report` at all.** It has no key, or it is `data-noloc`, or
  its text comes from a binding.
- **Text changes in the editor but not in a packaged build.** The `.locres` was not regenerated, or the
  culture is not in `CulturesToStage`.
- **The same string appears twice in a word count.** Your game's target is also gathering
  `Source/Generated`. Exclude it there.

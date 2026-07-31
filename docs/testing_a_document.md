# Testing a document

The single most important habit in Folio: **run with `--strict`**.

Nearly every RCSS mistake is a warning rather than an error in RmlUi. A misspelled property, a
decorator that does not exist, a `120ms` duration, a sprite that was never packed - all of them parse
cleanly and produce nothing. A check that only looks at errors will pass a thoroughly broken
stylesheet and you will find out in the engine, days later.

```bash
./bin/win64/uipreview.exe ui/screens/settings.rml --root . --strict
```

Non-zero exit means the document is not right. That is the whole contract.

## The tool

`uipreview` is the same layout engine the game runs, in a window you can open in half a second. It
watches the files it loaded and re-renders on save, so it is a live preview by default.

```bash
# Look at it.
./bin/win64/uipreview.exe ui/screens/settings.rml --root . --size 1920x1080

# Take a picture and exit.
./bin/win64/uipreview.exe ui/screens/settings.rml --root . --screenshot out.png

# Walk it with the keyboard, shooting each step.
./bin/win64/uipreview.exe ui/screens/settings.rml --root . --keys "down,down,right,enter" --shot-each out/
```

`--root` is the project directory, and it is what makes `/ui/...` and `/folio/...` resolve. Getting it
wrong is the most common reason nothing appears.

Most of the time you will not type any of this - the **[VSCode extension](vscode.md)** runs it beside
the document and adds click-to-find-the-rule on top.

## The checks

Each is a separate flag because each answers a different question, and running all of them on every
save would be slow for no gain.

| | |
|---|---|
| `--strict` | Warnings fail the run. Always on in CI, and worth on locally. |
| `--a11y` | Contrast, text size, hit targets, control names, focus visibility. See **[accessibility](accessibility.md)**. |
| `--culture pseudo` | Inflates and accents every string, which finds overflow before translation exists. Also enables the localisation lint - `--strict` alone does not run it. |
| `--dp-ratio 2` | The document at a different UI scale. Text wraps in different places at different ratios, by design; a container sized to fit its content exactly at one ratio will break at another. |
| `--input-device gamepad` | Pins what the document thinks the player is holding, so a gamepad screenshot needs no gamepad. |
| `--theme high-contrast` | Renders with a theme on. |

Run the two that matter together before committing a screen:

```bash
./bin/win64/uipreview.exe ui/screens/settings.rml --root . --strict --a11y --culture pseudo
```

## The reports

For anything a person should not have to eyeball.

**`--json-report`** — every diagnostic, machine readable, for CI.

**`--metrics-report`** — where every text element and every wrapped line landed. This is what makes a
change to the text engine reviewable: run it before and after and diff the numbers. Lines that broke
somewhere new are the regression most likely to happen and least likely to be spotted by eye. Compare
like for like - same size, same dp ratio.

**`--provenance-report`** — every element, its box, and every rule that reached it with the file and
line. This is what the VSCode extension uses to answer "which rule did this".

**`--a11y-report`** — the accessibility findings with the numbers rather than the prose.

## The harnesses

Three scripts, for three different questions.

**`./scripts/verify.sh`** — every document in `Tools/uipreview/tests/` under `--strict` at two dp
ratios, with metrics reports and screenshots. Diff a run against a previous one:

```bash
./scripts/verify.sh --out build/verify-before
# ...change something...
./scripts/verify.sh --baseline build/verify-before
```

**`./scripts/compare_renderers.sh`** — renders every document through *both* renderers and compares
where each text line landed. This is what backs the claim that the preview lays out identically to
the engine: not within a tolerance, exactly. Run it after touching anything in the text or layout
path.

**`./scripts/check_a11y.sh <dir>`** — every `.rml` under a directory, accessibility only, non-zero if
anything is found.

`./scripts/check_world_path.sh` is for anyone changing the renderer rather than a document - it
catches a property that the world-space path silently drops.

**`./scripts/compare_images.sh <a.png> <b.png>`** compares two shots numerically, which is the only
way anyone is going to notice that a shadow moved by two pixels.

```bash
./scripts/compare_images.sh before.png after.png --gate rgba --out diff.png
```

It reports max and mean difference for colour and for alpha **separately**, how many pixels are past
the tolerance, and the box containing them. `--out` writes a heat map: black where the two agree,
rising through red to white where they do not, which answers *where* rather than *how much*.

Alpha and colour are split because they mean different things here. **Across renderers, RGB will
never match** — the preview does no sRGB conversion where Slate linearises vertex colour and applies
display gamma — so gating on it produces a check nobody can keep green. Coverage is different: where
a glyph, a shadow or a mask puts its alpha is geometry, and geometry must match. `--gate rgba` is for
two shots from the *same* renderer, where any difference is a regression.

**One trap it warns about rather than hides:** a document with an opaque background has the same alpha
in every pixel, so `--gate alpha` compares nothing and passes whatever it is given. Compare on a
transparent ground, or gate on `rgb`.

`./scripts/check_headers.sh` catches a public header that includes a private one. The build cannot:
a module's own sources have `Private` on their include path, so it compiles here and fails only for
whoever consumes the plugin, naming the public header that looked fine.

**`./scripts/check_tests.sh`** runs the engine-side automation tests headlessly, which is everything
that needs a real UObject and so cannot live in the preview: today that is the view model bridge.
It finds the editor from the `.uproject`'s engine association, or `UE_ROOT` if you set one.

```bash
./scripts/check_tests.sh                        # everything under Folio.
./scripts/check_tests.sh --filter Folio.ViewModel
```

## What the preview is worth trusting

Worth being precise about, because the whole value of a preview is knowing where it lies.

**Layout matches the engine exactly.** `compare_renderers.sh` proves it across every document the
project ships. If a box is in the right place here, it is in the right place in the game.

**Colour will differ from a screenshot of the game, always.** The preview does no sRGB conversion
where Slate linearises vertex colour and applies display gamma. Judge colour in the engine.

**The appearance of layer effects is not compared.** `box-shadow`, `filter`, `backdrop-filter` and
`mask-image` are drawn in OpenGL here and replayed through RHI in the engine. Layout under an effect
is still exact; a shadow's blur radius is not something the preview can promise yet.

## In the engine

The preview does not replace looking at it in the game, it replaces looking at it in the game *first*.

| | |
|---|---|
| `folio.Reload` | Rebuild the open documents from disk. |
| `folio.ReloadTextures` | Pick up a re-baked atlas without reopening. RmlUi caches a texture by path forever otherwise. |
| `folio.Theme <name>` | Switch a theme live. |
| `stat Folio` | What the UI costs. See **[performance](performance_profiling.md)**. |
| `folio.Font.Report` | Which faces are loaded and what they cost. |

## If something does nothing

In this order:

1. **Look it up in [UI_CONVENTIONS.md](../UI_CONVENTIONS.md).** It is usually on the banned list.
   `display: grid`, `var()`, `background-image`, `text-shadow`, `outline`, `calc()`, `::before`.
2. **Run with `--strict`.** Most of the rest warns.
3. **Check the units.** `120ms` is not a duration - times are seconds. `rgba(0,0,0,0.5)` is fully
   transparent - alpha is 0-255. A bare number where a length is expected is a warning, not `px`.
4. **Check the easing name.** They are RmlUi's, not CSS's: `cubic-out`, not `ease-out`. A misspelled
   one is read as a property name and the transition silently never fires.
5. **Click it in the [VSCode preview](vscode.md)** and read the rules that actually reached it. Often
   the answer is that a rule you did not expect won.

## Where to go next

- **[Setting up VSCode](vscode.md)** — the same tool, without the command line.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** — what exists, what does not.
- **[Performance and profiling](performance_profiling.md)** — when it works but is slow.

# Testing a document

The most important habit in Folio: **run with `--strict`**.

Nearly every RCSS mistake is a warning rather than an error in RmlUi. A misspelled property, a decorator that does not exist, a `120ms` duration, a sprite that was never packed: all of them parse cleanly and produce nothing. A check that only looks at errors passes a thoroughly broken stylesheet.

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --strict
```

Non-zero exit means the document is not right.

## The tool

`uipreview` is the same layout engine the game runs, in a window that opens in half a second. It watches the files it loaded and re-renders on save.

```bash
# Look at it.
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --size 1920x1080

# Take a picture and exit.
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --screenshot out.png

# Walk it with the keyboard, shooting each step.
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --keys "down,down,right,enter" --shot-each out/

# One picture per frame from the first, for an animation a single capture cannot show.
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --filmstrip out/ --filmstrip-frames 30
```

`--root` is the project directory, and it makes `/UI/...` and `/folio/...` resolve. Getting it wrong is the most common reason nothing appears.

**[Folio Studio](studio.md)** draws with the same renderer and shows which rule set each property.

## The checks

| | |
|---|---|
| `--strict` | Warnings fail the run. Always on in CI, and locally. |
| `--a11y` | Contrast, text size, hit targets, control names, focus visibility. See **[accessibility](accessibility.md)**. |
| `--a11y-tree` | Prints what a screen reader reads out, in reading order: the same walk the engine builds its accessibility tree from. |
| `--a11y-contrast 7` | The ratio normal text is held to. 7 is what a high-contrast theme should meet. |
| `--culture pseudo` | Inflates and accents every string, which finds overflow before translation exists. Also enables the localisation lint, which `--strict` alone does not run. |
| `--dp-ratio 2` | The document at a different UI scale. Text wraps in different places at different ratios; a container sized to fit its content exactly at one ratio breaks at another. |
| `--input-device gamepad` | Pins what the document thinks the player is holding, so a gamepad screenshot needs no gamepad. |
| `--theme high-contrast` | Renders with a theme on. Pair it with `--a11y-contrast 7`. |
| `--text-scale 1.5` | The accessibility text scale. With `--a11y`, also reports the text it could not move and anything that stopped fitting. |
| `--reduced-motion` | Every transition, animation and smooth scroll off, as the setting does. |
| `--no-screen-effects` | Hides the layers marked `folio-screen-effect`. |
| `--bake <SEL>=<FILE>` | Renders one element alone, on nothing, cropped to its box, as a PNG with alpha. Repeatable. Refuses a layer effect and names the `folio.Bake` line to use in the editor instead, since the two renderers draw those differently. See [performance](performance_profiling.md). |
| `--text-speed 4`, `--instant-text` | How fast `<text>` types, and whether it types at all. Separate from reduced motion. |
| `--simulate deuteranopia` | Redraws captures, and re-runs the contrast pass, as that colour vision sees them. Also `protanopia`, `tritanopia`, `achromatopsia`. |
| `--flash-check` | Renders `--filmstrip-frames` frames and reports flashing over three a second, a seizure risk and a certification question. |

Before committing a screen:

```bash
./bin/win64/uipreview.exe UI/screens/settings.rml --root . --strict --a11y --culture pseudo
```

## The reports

**`--json-report`**: every diagnostic, machine readable, for CI.

**`--metrics-report`**: where every text element and every wrapped line landed. Run it before and after a text engine change and diff the numbers: lines that broke somewhere new are the likeliest regression and the hardest to spot by eye. Compare like for like: same size, same dp ratio.

**`--provenance-report`**: every element, its box, and every rule that reached it with the file and line. The VSCode extension uses it to answer "which rule did this".

**`--a11y-report`**: the accessibility findings with the numbers rather than the prose.

**`--a11y-tree-report`**: the screen reader tree with bounds, ranges and element paths. `verify.sh` compares `tests/a11y_tree.rml` against a committed transcript, so a change in what a document reads as fails a test.

## Output and exit codes

`uipreview` writes program output to stdout and diagnostics to stderr as `path:line:col: severity: [CODE] message`, which matches what editors and `grep` expect. Paths are relative to the content root, so output is identical across machines.

Exit codes: `0` ok, `1` document errors, `2` usage, `3` init failure, `4` runtime failure, `5` warnings present under `--strict`.

**Use `--strict` in CI.**

`uipreview --help` lists every flag.

## The harnesses

**`./scripts/verify.sh`**: every document in `Tools/uipreview/tests/` under `--strict` at two dp ratios, with metrics reports and screenshots. Diff a run against a previous one:

```bash
./scripts/verify.sh --out build/verify-before
# ...change something...
./scripts/verify.sh --baseline build/verify-before
```

**`./scripts/compare_renderers.sh`**: renders every document through *both* renderers and compares where each text line landed. This backs the claim that the preview lays out identically to the engine, not within a tolerance. Run it after touching anything in the text or layout path.

**`./scripts/check_a11y.sh <dir>`**: every `.rml` under a directory, accessibility only, non-zero if anything is found.

`./scripts/check_world_path.sh` is for anyone changing the renderer rather than a document: it catches a property the world-space path silently drops.

**`./scripts/compare_images.sh <a.png> <b.png>`** compares two shots numerically, which catches a shadow that moved by two pixels.

```bash
./scripts/compare_images.sh before.png after.png --gate rgba --out diff.png
```

It reports max and mean difference for colour and for alpha **separately**, how many pixels are past the tolerance, and the box containing them. `--out` writes a heat map: black where the two agree, rising through red to white where they do not.

**Across renderers, RGB never matches** (the preview does no sRGB conversion where Slate linearises vertex colour and applies display gamma), so do not gate on it. Where a glyph, a shadow or a mask puts its alpha is geometry, and geometry must match. `--gate rgba` is for two shots from the *same* renderer, where any difference is a regression.

**It warns about one trap:** a document with an opaque background has the same alpha in every pixel, so `--gate alpha` compares nothing and passes whatever it is given. Compare on a transparent ground, or gate on `rgb`.

`./scripts/check_recipes.sh` runs every shipped recipe under `--strict --a11y` four times: as authored, under the high-contrast theme at 7:1, at 150% text scale, and simulating deuteranopia. People copy them to start a screen, so a finding in one is inherited by everyone who uses it.

`./scripts/check_headers.sh` catches a public header that includes a private one. The build cannot: a module's own sources have `Private` on their include path, so it compiles here and fails only for whoever consumes the plugin, naming the public header that looked fine.

**`./scripts/check_tests.sh`** runs the engine-side automation tests headlessly, which is everything that needs a real UObject: the view model bridge. It finds the editor from the `.uproject`'s engine association, or `UE_ROOT` if you set one.

```bash
./scripts/check_tests.sh                        # everything under Folio.
./scripts/check_tests.sh --filter Folio.ViewModel
```

## What the preview is worth trusting

**Layout matches the engine exactly.** `compare_renderers.sh` proves it across every document the project ships.

**Colour always differs from a screenshot of the game.** The preview does no sRGB conversion where Slate linearises vertex colour and applies display gamma. Judge colour in the engine.

**The appearance of layer effects is not compared.** `box-shadow`, `filter`, `backdrop-filter` and `mask-image` are drawn in OpenGL here and replayed through RHI in the engine. Layout under an effect is still exact; a shadow's blur radius is not something the preview can promise.

## In the engine

| | |
|---|---|
| `folio.Reload` | Rebuild the open documents from disk. |
| `folio.ReloadTextures` | Pick up a re-baked atlas without reopening. Otherwise RmlUi caches a texture by path forever. |
| `folio.Theme <name>` | Switch a theme live. |
| `stat Folio` | What the UI costs. See **[performance](performance_profiling.md)**. |
| `folio.Font.Report` | Which faces are loaded and what they cost. |

## If something does nothing

In this order:

1. **Look it up in [UI_CONVENTIONS.md](../UI_CONVENTIONS.md).** It is usually on the banned list: `background-image`, `text-shadow`, `outline`, `clamp()`, `::before`.
2. **Run with `--strict`.** Most of the rest warns.
3. **Check the units.** `120ms` is not a duration: times are seconds. `rgba(0,0,0,0.5)` is fully transparent: alpha is 0-255. A bare number where a length is expected is a warning, not `px`.
4. **Check the easing name.** They are RmlUi's, not CSS's: `cubic-out`, not `ease-out`. A misspelled one is read as a property name and the transition silently never fires.
5. **Select it in [Folio Studio](studio.md)** and read the rule each value in **Properties** comes from. Often a rule you did not expect won.

## Where to go next

- **[Setting up VSCode](vscode.md)**: the same tool, without the command line.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)**: what exists, what does not.
- **[Performance and profiling](performance_profiling.md)**: when it works but is slow.

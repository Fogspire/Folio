# Folio

An RmlUi-based UI system for Unreal Engine 5, replacing UMG as the authoring layer.
Markup is plain text (`.rml` / `.rcss`), not hand-assembled widget hierarchies.

Two renderers draw the same markup: `uipreview`, a standalone tool with no Unreal dependency,
and the UE plugin under `Source/`. Anything authored in one renders in the other.

## Layout

| Path | What |
|---|---|
| `FOLIO_BRIEF.md` | The plan of record: phases, gates, decisions taken, and the feature backlog |
| `docs/` | Written for someone learning to use Folio, starting at `docs/hello_world.md` |
| `UI_CONVENTIONS.md` | **What RCSS actually supports.** Authoritative. Read before authoring markup |
| `NOTES.md` | Gotchas ledger. Read this before debugging anything surprising |
| `Source/Folio/` | The UE module: renderer, input, document lifecycle |
| `Source/FolioCore/` | Portable core shared by both renderers: font engine, distance fields, atlases |
| `Tools/uipreview/` | The standalone preview tool (C++20, CMake, GLFW + OpenGL 3.3) |
| `Tools/msdfbake/` | Bakes font atlases and vector sprite sheets offline |
| `Shaders/` | The distance field shader, shared by the material and the layer path |
| `rcss/` | `folio.rcss`, the base stylesheet every document links |
| `fonts/` | The variable typefaces Folio ships as defaults |
| `scripts/build.ps1` | The supported build entry point |
| `scripts/verify.ps1` | The verification harness |
| `ASSETS_WANTED.md` | Art request queue, maintained automatically by `uipreview` |

## Building

Requires **Visual Studio 2022** with the C++ desktop workload. Nothing else needs installing:
`build.ps1` locates the VS-bundled CMake and Ninja through `vswhere`, so neither has to be
on `PATH`.

```powershell
.\scripts\build.ps1                        # RelWithDebInfo, default
.\scripts\build.ps1 -Config Debug
.\scripts\build.ps1 -Clean                 # wipes the build tree, re-fetches dependencies
.\scripts\build.ps1 -Asan                  # AddressSanitizer, into its own build tree
```

Output lands at `bin\win64\`.

The first build clones the dependencies and takes several minutes. Later builds reuse them.

`build.ps1` runs `check_shader_parity.ps1` before compiling, which fails if the HLSL and GLSL
distance field shaders have drifted apart. `-SkipShaderParity` to bypass it while editing one
side.

Every configuration writes to the same `bin\win64\`, so an `-Asan` build replaces the normal
binaries until you rebuild without it. Instrumented binaries need the MSVC environment on PATH
for the sanitizer runtime.

## Verification

```powershell
.\scripts\verify.ps1 -Out build\verify-before
# ...change something...
.\scripts\verify.ps1 -Baseline build\verify-before
```

Runs every document in `Tools/uipreview/tests/` under `--strict` at two dp ratios, writes a text
metrics report for each, diffs them against the baseline if given, and takes the screenshots
that have to be judged by eye.

The metrics diff is the point. Advances here are unhinted and scaled from design units, so a
change to the text engine shifts them slightly and moves line breaks; the diff reports lines
that wrapped somewhere new. That is the regression this system is most likely to cause and the
least likely to be noticed by looking.

Use `-Asan` for any intermittent crash. Lifetime bugs here tend to read freed memory that is
still intact, so they survive for weeks and then fail on someone else's machine.

### Backend and dependency choices

**Backend: GLFW + OpenGL 3.3**, which is what upstream documents first. `RmlUi_Renderer_GL3`
is the only shipped renderer implementing the full 6.x feature set (render layers, filters,
clip masks, shaders); GL2 and the SDL renderer silently drop box-shadow, filters and gradients.
GLFW additionally allows a hidden window, which is how screenshots are captured without a
visible one.

**Dependencies come from CMake `FetchContent` rather than vcpkg**, which upstream suggests.
vcpkg would need bootstrapping and roughly a gigabyte of tooling before a first build, its RmlUi
port tracks whatever its baseline pins rather than the exact release we want, and its triplets
fix the CRT and toolset, which fights Phase 3 where RmlUi has to build with the same toolset as
Unreal. FetchContent builds from source with whatever toolset we set, pinned to a commit hash in
this file, with nothing installed globally.

Local fixes to a dependency go in `patches/<dependency>/*.patch` and are applied at fetch time.
See `patches/README.md`.

### Dependencies

Fetched by CMake at configure time, each pinned to an exact commit. No package manager, no
submodules, nothing installed globally.

| Dependency | Version |
|---|---|
| [RmlUi](https://github.com/mikke89/RmlUi) | 6.2 |
| [GLFW](https://github.com/glfw/glfw) | 3.4 |
| [FreeType](https://github.com/freetype/freetype) | 2.13.3 |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 |
| [lodepng](https://github.com/lvandeve/lodepng) | pinned commit |
| [msdfgen](https://github.com/Chlumsky/msdfgen) | 1.12 |
| [nanosvg](https://github.com/memononen/nanosvg) | pinned commit, vector baking only |

Skia's path operations are vendored in `Source/ThirdParty/SkiaSimplify/` rather than fetched.
msdfgen needs them to union a glyph's overlapping contours before generating a field — without
that, a stem and a bowl that intersect produce an inconsistent field, and the median
reconstructs a false edge through the middle of a solid stroke.

**RmlUi is pinned to 6.2 and that pin is deliberate.** The docs site at
<https://mikke89.github.io/RmlUiDoc/> is built from RmlUi's `master` branch and carries no
version banner, so it documents features that are not in any release. `NOTES.md` records the
specific ones that bite.

## Using `uipreview`

The tool is game agnostic: it renders whatever content root it is pointed at.

```powershell
# open a document with hot reload and the RmlUi debugger. F8 toggles the debugger
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --debug

# render one frame to a PNG without showing a window
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --screenshot out.png --size 1280x800

# drive navigation and capture each step, to verify focus order
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --keys "down,down,confirm" --shot-each shots\

# check a screen for length overflow before any translations exist
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --culture pseudo --screenshot pseudo.png

# validate for CI, failing on warnings too, and emit a machine readable report
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --screenshot nul --strict --json-report report.json
```

**Fonts.** Faces resolve in order: `--font` if given, else any font under `<root>/ui/fonts`,
else the variable typefaces Folio ships in `fonts/`, else the engine's Roboto from
`Engine/Content/Slate/Fonts`. The engine is found automatically, or named with `--engine` or
`UE_ENGINE_DIR`. The same order applies in engine.

Folio bundles Inter and RobotoMono, both variable, because variable-font support is most of the
value of the text engine and the engine's Roboto is a static face. A game that drops its own
faces in `ui/fonts/` still wins outright. `NOTES.md` records why this reverses the brief.

**Text is always distance-field.** Glyphs are MTSDF, generated once per face into a
size-independent atlas, so text stays sharp at any size, transform scale or DPI. `font-weight`
drives the `wght` axis directly rather than snapping to the nearest cut. Font effects resolve
from the field rather than baking extra atlases. `UI_CONVENTIONS.md` §6 has the constraints.

**The base stylesheet.** RmlUi has no user-agent stylesheet and defaults every element to
`display: inline`, so Folio ships one. Every document should start with:

```html
<link type="text/rcss" href="/folio/folio.rcss"/>
```

`/folio/` resolves against the plugin rather than the content root, so it works from anywhere.
It establishes block display and gives the document a width, and deliberately sets no colours,
spacing or typography, which belong to the game.

`uipreview --help` lists every flag.

**Hot reload granularity.** An `.rcss` edit reloads stylesheets only, so the DOM, focus, scroll
position and data model are untouched. Only an `.rml` edit rebuilds the document, capturing and
restoring focus and scroll offsets around it. Editing the `--data` JSON re-reads the model on
its own.

**Output.** Program output goes to stdout; diagnostics go to stderr as
`path:line:col: severity: [CODE] message`, which matches what editors and `grep` expect. Paths
are relative to the content root, so output is identical across machines.

Exit codes: `0` ok, `1` document errors, `2` usage, `3` init failure, `4` runtime failure,
`5` warnings present under `--strict`.

Because nearly every RCSS problem is a warning rather than an error in RmlUi, a validity check
that only looks at errors will pass a thoroughly broken stylesheet. Use `--strict` in CI.

**Localization lint.** `--strict` also reports text that carries no `data-loc` key and so cannot
be translated, but only when a culture is loaded, so pair it with `--culture pseudo`. Text
produced by a data binding is exempt, as is anything marked `data-noloc` for strings that are
not language: numerals, timers, player names.

## Art that does not exist yet

Markup may reference **sprite names only**, never image paths. A name that no loaded
`@spritesheet` declares renders as a magenta checkerboard with the name stamped on it, warns,
and is recorded in `ASSETS_WANTED.md` next to the size it was actually drawn at. Screens can
therefore be built, navigated and animated before any art exists, and art gets produced in
batches against that file rather than per screen.

`ASSETS_WANTED.md` is rewritten in place between its markers, so rerunning with no changes
leaves the file byte identical. Notes written below the markers are preserved.

Textures may be **PNG or uncompressed TGA**. RmlUi itself only reads TGA; `uipreview` decodes
PNG through lodepng and premultiplies it, since 6.x expects premultiplied texture data.

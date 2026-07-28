# Folio

An RmlUi-based UI system for Unreal Engine 5, replacing UMG as the authoring layer.
Markup is plain text (`.rml` / `.rcss`), not hand-assembled widget hierarchies.

Currently at **Phase 1**: `uipreview`, a standalone preview tool with no Unreal dependency.
The UE plugin itself (`Source/`, `Folio.uplugin`) arrives in Phase 3.

## Layout

| Path | What |
|---|---|
| `Tools/uipreview/` | The standalone preview tool (C++20, CMake, GLFW + OpenGL 3.3) |
| `ui/` | Markup: components, tokens, examples, fonts, mock data, translations |
| `scripts/build.ps1` | The supported build entry point |
| `NOTES.md` | Gotchas ledger. Read this before debugging anything surprising |
| `ASSETS_WANTED.md` | Art request queue, maintained automatically by `uipreview` |

## Building

Requires **Visual Studio 2022** with the C++ desktop workload. Nothing else needs installing:
`build.ps1` locates the VS-bundled CMake and Ninja through `vswhere`, so neither has to be
on `PATH`.

```powershell
.\scripts\build.ps1                        # RelWithDebInfo, default
.\scripts\build.ps1 -Config Debug
.\scripts\build.ps1 -Clean                 # wipes the build tree, re-fetches dependencies
```

Output lands at `bin\win64\uipreview.exe`.

The first build clones five dependencies and takes several minutes. Later builds reuse them.

### Backend and dependency choices

**Backend: GLFW + OpenGL 3.3**, which is what upstream documents first. `RmlUi_Renderer_GL3`
is the only shipped renderer implementing the full 6.x feature set (render layers, filters,
clip masks, shaders); GL2 and the SDL renderer silently drop box-shadow, filters and gradients.
GLFW additionally allows a hidden window, which is how screenshots are captured without a
visible one.

**Dependencies come from CMake `FetchContent` rather than vcpkg**, which upstream suggests.
vcpkg would need bootstrapping and roughly a gigabyte of tooling before a first build, and its
RmlUi port tracks whatever its baseline pins rather than the exact release we want. FetchContent
pins each dependency to a commit hash in this file with no global installation. The tradeoff is
that patching a dependency is awkward; if RmlUi ever needs a local patch, that one dependency
should become a submodule at that point.

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

**RmlUi is pinned to 6.2 and that pin is deliberate.** The docs site at
<https://mikke89.github.io/RmlUiDoc/> is built from RmlUi's `master` branch and carries no
version banner, so it documents features that are not in any release. `NOTES.md` records the
specific ones that bite.

## Using `uipreview`

The tool is game agnostic: it renders whatever content root it is pointed at. Folio itself
ships no markup and no typeface, so a font has to be supplied.

```powershell
$roboto = "..\..\..\UnrealEngine-Ninja\Engine\Content\Slate\Fonts\Roboto-Regular.ttf"

# open a document with hot reload and the RmlUi debugger. F8 toggles the debugger
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --font $roboto --debug

# render one frame to a PNG without showing a window
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --font $roboto --screenshot out.png --size 1280x800

# drive navigation and capture each step, to verify focus order
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --font $roboto --keys "down,down,confirm" --shot-each shots\

# check a screen for length overflow before any translations exist
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --font $roboto --culture pseudo --screenshot pseudo.png

# validate for CI, failing on warnings too, and emit a machine readable report
.\bin\win64\uipreview.exe ui\screens\main_menu.rml --font $roboto --screenshot nul --strict --json-report report.json
```

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

## Art that does not exist yet

Markup may reference **sprite names only**, never image paths. A name that no loaded
`@spritesheet` declares renders as a magenta checkerboard with the name stamped on it, warns,
and is recorded in `ASSETS_WANTED.md` next to the size it was actually drawn at. Screens can
therefore be built, navigated and animated before any art exists, and art gets produced in
batches against that file rather than per screen.

`ASSETS_WANTED.md` is rewritten in place between its markers, so rerunning with no changes
leaves the file byte identical. Notes written below the markers are preserved.

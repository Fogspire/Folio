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

```powershell
# open a document with hot reload and the RmlUi debugger
.\bin\win64\uipreview.exe ui\examples\hello.rml --debug

# render one frame headless to a PNG
.\bin\win64\uipreview.exe ui\examples\hello.rml --screenshot out.png --size 1280x800

# drive navigation and capture each step, to verify focus order
.\bin\win64\uipreview.exe ui\examples\menu.rml --keys "down,down,confirm" --shot-each shots\

# check a document for length overflow before translations exist
.\bin\win64\uipreview.exe ui\examples\menu.rml --culture pseudo --screenshot pseudo.png
```

`uipreview --help` lists every flag. Diagnostics go to stderr in
`path:line:col: severity: [CODE] message` form; program output goes to stdout.

Exit codes: `0` ok, `1` document errors, `2` usage, `3` init failure, `4` runtime failure,
`5` warnings present under `--strict`.

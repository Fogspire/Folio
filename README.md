# Folio

An RmlUi-based UI system for Unreal Engine 5, replacing UMG as the authoring layer.
Markup is plain text (`.rml` / `.rcss`), not hand-assembled widget hierarchies.

Two renderers draw the same markup: `uipreview`, a standalone tool with no Unreal dependency,
and the UE plugin under `Source/`. Anything authored in one renders in the other.

Text and vector art are distance fields, so a screen stays sharp at any size, transform scale or DPI.
The same document runs diegetically in the world, readable and interactive from across the room.

---

**Using Folio?** This file is about the repository. Everything about building UI is in **[docs/](docs/README.md)**.

| | |
|---|---|
| **[docs/README.md](docs/README.md)** | The documentation index. Start here |
| **[docs/hello_world.md](docs/hello_world.md)** | Install it and get a document on screen |
| **[UI_CONVENTIONS.md](UI_CONVENTIONS.md)** | **What RCSS actually supports.** Authoritative. Read before authoring markup |
| **[AGENTS.md](AGENTS.md)** | The cold-start brief for an agent asked to build UI in a project it has not seen |

---

## Layout

| Path | What |
|---|---|
| `FOLIO_BRIEF.md` | The plan of record: phases, gates, decisions taken, and the feature backlog |
| `NOTES.md` | Gotchas ledger. Read this before debugging anything surprising |
| `Source/Folio/` | The UE module: renderer, input, document lifecycle |
| `Source/FolioCore/` | Portable core shared by both renderers: font engine, distance fields, atlases |
| `Tools/uipreview/` | The standalone preview tool (C++20, CMake, GLFW + OpenGL 3.3) |
| `Tools/msdfbake/` | Bakes font atlases and vector sprite sheets offline |
| `Tools/vscode-folio/` | The editor extension: highlighting, live preview, click-to-find-the-rule |
| `Shaders/` | The distance field shader, shared by the material and the layer path |
| `rcss/` | `folio.rcss`, the component library, the default theme, and the recipes |
| `fonts/` | The variable typefaces Folio ships as defaults |
| `examples/` | Documents to look at and copy |
| `dev/` | Fixtures the console commands and verify scripts load. Not examples |
| `scripts/build.sh` | The supported build entry point |
| `scripts/verify.sh` | The verification harness |
| `ASSETS_WANTED.md` | Art request queue, maintained automatically by `uipreview` |

## Building

Requires **Visual Studio 2022** with the C++ desktop workload. Nothing else needs installing:
`build.sh` locates the VS-bundled CMake and Ninja through `vswhere`, so neither has to be
on `PATH`.

```bash
./scripts/build.sh                        # RelWithDebInfo, default
./scripts/build.sh --config Debug
./scripts/build.sh --clean                 # wipes the build tree, re-fetches dependencies
./scripts/build.sh --asan                  # AddressSanitizer, into its own build tree
```

Output lands at `bin/win64`.

The first build clones the dependencies and takes several minutes. Later builds reuse them.

`build.sh` runs `check_shader_parity.sh` before compiling, which fails if the HLSL and GLSL
distance field shaders have drifted apart. `--skip-shader-parity` to bypass it while editing one
side.

Every configuration writes to the same `bin/win64`, so an `--asan` build replaces the normal
binaries until you rebuild without it. Instrumented binaries need the MSVC environment on PATH
for the sanitizer runtime.

## Verification

```bash
./scripts/verify.sh --out build\verify-before
# ...change something...
./scripts/verify.sh --baseline build\verify-before
```

Runs every document in `Tools/uipreview/tests/` under `--strict` at two dp ratios, writes a text
metrics report for each, diffs them against the baseline if given, and takes the screenshots
that have to be judged by eye.

The metrics diff is the point. Advances here are unhinted and scaled from design units, so a
change to the text engine shifts them slightly and moves line breaks; the diff reports lines
that wrapped somewhere new. That is the regression this system is most likely to cause and the
least likely to be noticed by looking.

The other harnesses, each answering a different question:

| | |
|---|---|
| `scripts/compare_renderers.sh` | Renders every document through both renderers and compares where each text line landed. This is what backs the claim that the preview lays out exactly as the engine does |
| `scripts/check_world_path.sh` | Catches a property the world-space path silently drops |
| `scripts/check_a11y.sh` | Contrast, hit targets, control names and focus visibility over a directory |
| `scripts/check_invalidation.sh` | Proves an idle document stops doing work |

Use `--asan` for any intermittent crash. Lifetime bugs here tend to read freed memory that is
still intact, so they survive for weeks and then fail on someone else's machine.

`docs/testing_a_document.md` covers the same tools from the point of view of someone authoring a
screen rather than changing the renderer.

## Backend and dependency choices

**Backend: GLFW + OpenGL 3.3**, which is what upstream documents first. `RmlUi_Renderer_GL3`
is the only shipped renderer implementing the full 6.x feature set (render layers, filters,
clip masks, shaders); GL2 and the SDL renderer silently drop box-shadow, filters and gradients.
GLFW additionally allows a hidden window, which is how screenshots are captured without a
visible one.

**Dependencies come from CMake `FetchContent` rather than vcpkg**, which upstream suggests.
vcpkg would need bootstrapping and roughly a gigabyte of tooling before a first build, its RmlUi
port tracks whatever its baseline pins rather than the exact release we want, and its triplets
fix the CRT and toolset, which fights the UE build where RmlUi has to use the same toolset as
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

## Diagnostics

`uipreview` writes program output to stdout and diagnostics to stderr as
`path:line:col: severity: [CODE] message`, which matches what editors and `grep` expect. Paths
are relative to the content root, so output is identical across machines.

Exit codes: `0` ok, `1` document errors, `2` usage, `3` init failure, `4` runtime failure,
`5` warnings present under `--strict`.

**Use `--strict` in CI.** Nearly every RCSS problem is a warning rather than an error in RmlUi, so a
validity check that only looks at errors will pass a thoroughly broken stylesheet.

`uipreview --help` lists every flag.

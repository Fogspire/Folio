# Folio

**Game UI for Unreal Engine 5, written as markup and stylesheets instead of widget graphs.** You author in HTML- and CSS-like text, see the real result outside the engine as you type, and ship it on screen or on any surface in the world, pin-sharp at every size.

Folio is free, source included, and built to be the UI layer of a shipping game, from a solo project to a full production.

## Why Folio

- **Absolutely rapid, and outside Unreal.** Everything is a text file. Edit it in **[Folio Studio](docs/studio.md)**, a WYSIWYG editor that draws with the same renderer the game does. The UI designer does not need Unreal installed. A polished, animated menu is minutes of markup, where the same thing in UMG or Slate is days of widget wrangling.
- **Unparalleled clarity.** Text and vector art are distance fields, resolved for the exact pixel they land on. A screen is as sharp at 4K as at 720p, under `transform: scale(8)`, mid-animation, and from across a room.
- **Text that scales without limit.** One field per glyph serves every size, so there is no atlas per font size and no blur when text grows, shrinks or rotates. Variable-weight fonts, outlines and glows are part of the same field. Slate rasterises a bitmap for each size; Folio never does.
- **Vector art.** SVG icons bake to the same fields, so an icon drawn once is correct at any size and can be recoloured from the stylesheet.
- **UI in the world that stays sharp.** A text document on a computer screen, a sign or a cockpit panel is drawn after the tonemapper by default, where TAA, TSR, motion blur and post-processing cannot touch it. World UI in Unreal normally smears the moment the camera moves; this does not. It is still depth-tested, so it goes behind geometry correctly. Two other modes put it in the scene to take bloom and grading, or onto curved meshes.
- **World screens the player can use.** Point at one, click it, navigate it with a pad, type into it. A working in-game computer only involved a document and a component, not an entire project. See the [CRT terminal tutorial](docs/tutorials/crt_terminal.md).
- **Stereo.** World documents render correctly under instanced stereo, verified with Unreal's emulated stereo. It has not yet been run on a headset.
- **Animation built in.** Transitions, keyframes, springs that respond to the camera, and a shader decorator for scan lines, sweeps and additive glow.
- **Plain text in source control.** A screen is not a binary `.uasset`. It diffs, merges and reviews like code, two people can work on one screen without locking it, and plain git is enough. A visual edit in Studio changes only the lines it has to.
- **Keyboard, mouse and gamepad from one stylesheet.** Spatial navigation, focus, modals and input prompts that swap when the player picks up a pad, with no focus code to write.
- **Driven by your game.** Bind any `UObject` by reflection: `{{ Health }}` in text, lists from arrays, commands back to C++ or Blueprint. No JavaScript, and no binding code.
- **Quick to prototype with an AI agent, if you use one.** A screen is plain text, and the checker and screenshot tool tell an agent what it got wrong without a person in the loop, so a short prompt can come back as a finished, working screen. Hand it [AGENTS.md](AGENTS.md).

## In the box

Working screens to copy, each passing the strict and accessibility checks, with the tutorials that build them:

| | |
|---|---|
| Menus | Main menu, pause menu, confirmation dialog, modal focus trapping, a controls screen that rebinds against Enhanced Input |
| HUD | Nameplates and world markers, bound input display, a helmet HUD that lags and settles with the camera, damage and low-health screen edges |
| Dialogue | A typewriter, rich text with emphasis inside one translatable line, shared names for places and characters, a full dialogue system |
| Inventory | A grid with drag and drop, 3D items in cells, a paper doll, a list of ten thousand items that allocates nothing while scrolling |
| In the world | A CRT terminal with draggable, resizable windows and a text field, an LCD terminal |
| Captions | Stacked closed captions in the classic shooter style: several speakers at once, each line on its own timer, the newest bright and older lines dimmed, a name and colour per speaker, and sounds set apart from speech |
| Components | Buttons, sliders, toggles, tab bars, lists, panels, scrollbars, a default theme, drifting motes |
| Authoring | Folio Studio, `uipreview` for checks and screenshots in a build, vector and font baking, view model code generation, VSCode highlighting |

The manual starts at **[docs/README.md](docs/README.md)**.

## For a production

- **Accessibility.** A screen reader that reads menus aloud, and text scale, reduced motion and high-contrast settings. Contrast, hit-target size, focus visibility, control names and flashing are checked by the build, and a colour-vision simulation shows a screen as one player in twelve sees it. See [accessibility](docs/accessibility.md).
- **Real text shaping.** HarfBuzz shapes every string: ligatures, contextual forms, combining marks, GPOS kerning and right-to-left scripts.
- **Localisation.** Strings are keyed in markup and gathered into Unreal's own localisation pipeline, with pseudo-localisation to find the layout that only fits English before translation exists.
- **A build gate.** `--strict` fails the build on a stylesheet mistake, the accessibility checks run beside it, and a verification harness diffs text metrics and screenshots between changes. The engine side carries its own automation tests.
- **Two renderers, one result.** The standalone preview and the engine share the font engine, the distance fields and the shader maths, and a parity check fails the build if the two drift.
- **Performance you can see.** A document that has not changed costs nothing per frame. `stat Folio` and Unreal Insights scopes cover the whole path, and expensive effects bake to textures.
- **Fits the engine you have.** Hosts inside an existing UMG or CommonUI tree, so CommonUI activatable widgets keep working as the screen stack and input handler around a document, binds MVVM view models or plain objects, reads Enhanced Input, and stages documents for packaging. Documents are loose files, so UI can be changed on a device without cooking again.
- **Honest limits.** [What Folio does not do yet](docs/known_gaps.md) lists every known gap in one place, so nothing is discovered late.
- **Source and licence.** All source is here. Use it in commercial games and change it freely; see [License](#license).

## Get started

| | |
|---|---|
| **[Hello world](docs/hello_world.md)** | Install the plugin and get a document on screen |
| **[Folio Studio](docs/studio.md)** | The visual editor |
| **[UI_CONVENTIONS.md](UI_CONVENTIONS.md)** | What the stylesheet language supports. It is close to CSS and is not CSS; read this before writing any |
| **[AGENTS.md](AGENTS.md)** | The brief to hand an AI agent asked to build UI with Folio |

## Platform support

### Developing

| | Windows | macOS | Linux |
|---|---|---|---|
| The plugin in the Unreal editor | Supported | Beta | Beta |
| Building its libraries (`build_ue_libs.sh`) | Supported | Beta | Beta |
| Folio Studio | Supported | Beta | Beta |
| `uipreview` | Supported | Not ported | Not ported |

### Shipping

| Platform | |
|---|---|
| Windows | Supported |
| macOS, Linux | Beta |
| Nintendo Switch | Runs on a devkit |
| Other consoles | Not tried |

Folio is developed on Windows. The aim is for it to work as well everywhere else, so treat each new release as beta on macOS and Linux, and open an [issue](https://github.com/Fogspire/Folio/issues) when something does not work there. For a console, an issue is welcome too: support will be attempted where it can be.

---

The rest of this file is about the repository itself.

## Layout

| Path | What |
|---|---|
| `Source/Folio/` | The UE module: renderer, input, document lifecycle |
| `Source/FolioCore/` | Portable core shared by both renderers: font engine, distance fields, atlases |
| `Source/FolioEditor/` | Details-panel customisations: the document picker and the density button |
| `Tools/uipreview/` | The renderer as a command-line tool: checks, screenshots and baking, for a build or an agent (C++20, CMake, GLFW + OpenGL 3.3) |
| `Tools/msdfbake/` | Bakes font atlases and vector sprite sheets offline |
| `Tools/vscode-folio/` | The VSCode extension: highlighting, and a preview deprecated in favour of Studio |
| `Tools/studio/` | Folio Studio, the visual editor |
| `Shaders/` | The distance field shader, shared by the material and the layer path |
| `rcss/` | `folio.rcss`, the component library, the default theme, and the recipes |
| `fonts/` | The variable typefaces Folio ships as defaults |
| `examples/` | Documents to look at and copy |
| `docs/` | The manual. `docs/README.md` is the index |
| `scripts/build.sh` | The supported build entry point |
| `scripts/verify.sh` | The verification harness |

## Building

Requires **Visual Studio 2022** with the C++ desktop workload. Nothing else needs installing: `build.sh` locates the VS-bundled CMake and Ninja through `vswhere`, so neither has to be on `PATH`.

```bash
./scripts/build.sh                        # RelWithDebInfo, default
./scripts/build.sh --config Debug
./scripts/build.sh --clean                 # wipes the build tree, re-fetches dependencies
./scripts/build.sh --asan                  # AddressSanitizer, into its own build tree
```

Output lands at `bin/win64`.

The first build clones the dependencies and takes several minutes. Later builds reuse them.

`build.sh` runs `check_shader_parity.sh` before compiling, which fails if the HLSL and GLSL distance field shaders have drifted apart. `--skip-shader-parity` to bypass it while editing one side.

Every configuration writes to the same `bin/win64`, so an `--asan` build replaces the normal binaries until you rebuild without it. Instrumented binaries need the MSVC environment on PATH for the sanitizer runtime.

## Verification

```bash
./scripts/verify.sh --out build\verify-before
# ...change something...
./scripts/verify.sh --baseline build\verify-before
```

Runs every document in `Tools/uipreview/tests/` under `--strict` at two dp ratios, writes a text metrics report for each, diffs them against the baseline if given, and takes the screenshots that have to be judged by eye.

The metrics diff is the point. Advances here are unhinted and scaled from design units, so a change to the text engine shifts them slightly and moves line breaks; the diff reports lines that wrapped somewhere new. That is the regression this system is most likely to cause and the least likely to be noticed by looking.

The other harnesses, each answering a different question:

| | |
|---|---|
| `scripts/compare_renderers.sh` | Renders every document through both renderers and compares where each text line landed. This is what backs the claim that the preview lays out exactly as the engine does |
| `scripts/check_world_path.sh` | Catches a property the world-space path silently drops |
| `scripts/check_a11y.sh` | Contrast, hit targets, control names and focus visibility over a directory |
| `scripts/check_doc_samples.sh` | Whether the C++ samples in `docs/` are still in step with the file the build compiles |
| `scripts/check_invalidation.sh` | Proves an idle document stops doing work |

Use `--asan` for any intermittent crash. Lifetime bugs here tend to read freed memory that is still intact, so they survive for weeks and then fail on someone else's machine.

`docs/testing_a_document.md` covers the same tools from the point of view of someone authoring a screen rather than changing the renderer.

## Backend and dependency choices

**Backend: GLFW + OpenGL 3.3**, which is what upstream documents first. `RmlUi_Renderer_GL3` is the only shipped renderer implementing the full 6.x feature set (render layers, filters, clip masks, shaders); GL2 and the SDL renderer silently drop box-shadow, filters and gradients. GLFW additionally allows a hidden window, which is how screenshots are captured without a visible one.

**Dependencies come from CMake `FetchContent` rather than vcpkg**, which upstream suggests. vcpkg would need bootstrapping and roughly a gigabyte of tooling before a first build, its RmlUi port tracks whatever its baseline pins rather than the exact release we want, and its triplets fix the CRT and toolset, which fights the UE build where RmlUi has to use the same toolset as Unreal. FetchContent builds from source with whatever toolset we set, pinned to a commit hash in this file, with nothing installed globally.

Local fixes to a dependency go in `patches/<dependency>/*.patch` and are applied at fetch time. See `patches/README.md`.

### Dependencies

Fetched by CMake at configure time, each pinned to an exact commit. No package manager, no submodules, nothing installed globally.

| Dependency | Version |
|---|---|
| [RmlUi](https://github.com/mikke89/RmlUi) | 6.2 |
| [GLFW](https://github.com/glfw/glfw) | 3.4 |
| [FreeType](https://github.com/freetype/freetype) | 2.13.3 |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | 2.4.0, matching the copy Unreal ships |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 |
| [lodepng](https://github.com/lvandeve/lodepng) | pinned commit |
| [msdfgen](https://github.com/Chlumsky/msdfgen) | 1.12 |
| [nanosvg](https://github.com/memononen/nanosvg) | pinned commit, vector baking only |

Skia's path operations are vendored in `Source/ThirdParty/SkiaSimplify/` rather than fetched. msdfgen needs them to union a glyph's overlapping contours before generating a field — without that, a stem and a bowl that intersect produce an inconsistent field, and the median reconstructs a false edge through the middle of a solid stroke.

**RmlUi is pinned to 6.2 and that pin is deliberate.** The docs site at
<https://mikke89.github.io/RmlUiDoc/> is built from RmlUi's `master` branch and carries no
version banner, so it documents features that are not in any release. `docs/internals.md` records the specific ones that bite.

## Diagnostics

`uipreview` writes program output to stdout and diagnostics to stderr as `path:line:col: severity: [CODE] message`, which matches what editors and `grep` expect. Paths are relative to the content root, so output is identical across machines.

Exit codes: `0` ok, `1` document errors, `2` usage, `3` init failure, `4` runtime failure, `5` warnings present under `--strict`.

**Use `--strict` in CI.** Nearly every RCSS problem is a warning rather than an error in RmlUi, so a validity check that only looks at errors will pass a thoroughly broken stylesheet.

`uipreview --help` lists every flag.

## License

[MIT with the Commons Clause](LICENSE). In short:

| You may | |
|---|---|
| Use Folio in any game, commercial or not | Yes |
| Change it for your game | Yes |
| Publish your fork | Yes, under the same license |
| Sell Folio, or a product or service whose value is mostly Folio | No |

For a commercial license, open an issue or ask the author.

Contributions are accepted only with the right for the author to relicense them, so a commercial license can cover the whole of Folio.

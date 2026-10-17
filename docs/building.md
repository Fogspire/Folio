# Building Folio

For changing Folio itself. Authoring documents needs none of this: see [testing a document](testing_a_document.md).

## Build the tools

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

### Linux (cloud sessions, CI)

`build.sh` is Windows-only; on Linux, call CMake directly. GLFW needs the X11 headers, and with no display the preview runs under Xvfb.

```bash
apt-get install -y ninja-build libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev xvfb
cmake -S . -B build/linux -G Ninja -DGLFW_BUILD_WAYLAND=OFF
cmake --build build/linux --target uipreview
xvfb-run -a ./bin/linux/uipreview ../../UI/screens/pause.rml --root ../.. --strict --a11y
```

Output lands at `bin/linux`.

## Before you commit

Run the verification harness before and after a change, and diff the two:

```bash
./scripts/verify.sh --out build/verify-before
# ...change something...
./scripts/verify.sh --baseline build/verify-before
```

[Testing a document](testing_a_document.md#the-harnesses) covers what it checks and the other harnesses. Two more matter only when changing Folio:

| | |
|---|---|
| `scripts/check_doc_samples.sh` | Whether the C++ samples in `docs/` are still in step with the file the build compiles |
| `scripts/check_invalidation.sh` | Proves an idle document stops doing work |

Use `--asan` for any intermittent crash. Lifetime bugs here tend to read freed memory that is still intact, so they survive for weeks and then fail on someone else's machine.

## Where things are

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

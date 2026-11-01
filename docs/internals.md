# Internals

**Not for authoring UI.** This is the ledger for whoever maintains Folio's C++: why the code is shaped the way it is, and what has already been learned about RmlUi, Unreal's renderer and the build. Read it before debugging anything surprising, and before assuming awkward-looking code is arbitrary.

Everything an author needs is in **[the manual](README.md)**, and what Folio cannot do is in **[known gaps](known_gaps.md)**.

## RmlUi version and documentation

**Pinned to RmlUi 6.2** (released 2026-01-11).

**The docs site lies about what is in the release.** <https://mikke89.github.io/RmlUiDoc/> is built from RmlUi's `master` with no version banner, so features merged after 6.2 are documented as if they exist. Check `changelog.md` in the pinned source tree, not the website.

Known cases:

- **CSS custom properties and `var()` do not exist in 6.2.** Merged to master 2026-06-07 (PR #947) with follow-up bugs still open (#965 transitions with `var()`, #966 `var()` with dp-ratio changes). `rcss/themes/default.rcss` therefore expresses design tokens as **concrete classes**, not variables. Revisit at RmlUi 7.0.
- **`:disabled` is set from the `disabled` attribute on any element** (patch 0006). `:focus`, `:focus-visible`, `:checked`, `:hover`, `:active`, `:nth-child()`, `:not()` and `:scope` are all supported.
- **`Rml::ReleaseMemoryPools()` does not exist** in 6.2, so the reload path cannot use it.

## The RmlUi fork this project was first pointed at is dead

`git@github.com:viciious/RmlUi.git` was last pushed **August 2019**. It predates RmlUi 3.0: no flexbox, no media queries, no data bindings, and the pre-6.0 immediate-mode render interface. Folio builds against upstream `mikke89/RmlUi`.

## Backend choice: GLFW + OpenGL 3.3

`RmlUi_Renderer_GL3` is the only shipped renderer implementing the full 6.x feature set: render layers, `CompileFilter`, `CompileShader`, clip masks, `SaveLayerAsTexture`. GL2 and the SDL_Renderer backend silently drop box-shadow, filters and gradients, which the component library depends on. GLFW also gives a hidden-window mode for headless screenshots.

**The backend is forked** into `Tools/uipreview/src/backend/` because the upstream one constructs its `RenderInterface_GL3` in a file-static struct with no seam for a subclass, creates its window without letting us set `GLFW_VISIBLE`, and does end-frame and buffer-swap atomically (screenshot capture has to happen between the two). The fork records its upstream commit in a header comment and stays minimally diverged so diffs stay readable across an RmlUi bump.

## Dependencies come from FetchContent, not vcpkg

Fetched by CMake at configure time, each pinned to an exact commit in `cmake/Pins.cmake`. No package manager, no submodules, nothing installed globally.

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

Not vcpkg, though upstream RmlUi suggests it: it needs bootstrapping and roughly a gigabyte of tooling before a first build, its RmlUi port tracks its baseline rather than the exact release, and its triplets fix the CRT and toolset, while RmlUi has to use the same toolset as Unreal. FetchContent builds from source with whatever toolset is set.

Local fixes to a dependency go in `patches/<dependency>/*.patch` and are applied at fetch time. See `patches/README.md`.

Skia's path operations are vendored in `Source/ThirdParty/SkiaSimplify/` rather than fetched. msdfgen needs them to union a glyph's overlapping contours before generating a field: without that, a stem and a bowl that intersect produce an inconsistent field, and the median reconstructs a false edge through the middle of a solid stroke.

## Which advanced rendering features Folio implements

**Target: all of them.** Everything except the non-blur backdrop filters is in. Status table at the end of this section.

The Unreal render interface implements the complete RmlUi 6.x optional feature set. An absent feature does not fail loudly: it parses and draws nothing, so it is found months later as "why is this panel flat". Retrofitting one also means revisiting every document authored against its absence.

| Render interface feature | What stops working without it |
|---|---|
| `EnableClipMask` / `RenderToClipMask` | `border-radius` and `overflow: hidden` on anything transformed. Clips to the wrong rectangle |
| Render layers (`PushLayer` / `PopLayer` / `CompositeLayers`) | `opacity` on a subtree, `filter`, `backdrop-filter`, `mask-image` |
| `SaveLayerAsTexture` | `backdrop-filter`, and any effect reading what is already drawn |
| `CompileFilter` / `RenderFilter` | `filter` and `backdrop-filter` entirely: blur, drop-shadow, brightness, contrast, grayscale, invert, opacity, saturate, sepia, hue-rotate |
| `CompileShader` / `RenderShader` | Every gradient decorator, and any custom decorator |
| `SetTransform` | `transform`, so every animation that is not opacity or colour |

`box-shadow` needs no interface feature of its own but is drawn through the layer and clip mask paths, so it comes with them.

### Current status

| Feature | State |
|---|---|
| Geometry, textures, scissor, `transform` | Done, through `MakeCustomVerts` |
| Gradients (every `*-gradient` decorator) | Done, by baking the gradient into a texture rather than compiling a shader. The decorator's texture coordinates are pixel offsets inside the element box, the same space the shader parameters use, so the gradient can be evaluated on the CPU once and sampled |
| Clip masks | Done. A real stencil on the layer path, so `SetInverse` is exact |
| Render layers | Done. Render targets recorded on the game thread and replayed with RHI on the render thread, one pass per layer. Pooled by size |
| `filter: blur` | Done. A separable gaussian global shader over the layer target |
| `backdrop-filter: blur` | Done, via a Slate post-process pass, so the 3D scene behind a panel blurs with it. The region is a rectangle, so a rounded corner blurs square |
| `box-shadow` | Done |
| Text, font effects, vector sprites | Done. Distance fields through a UI-domain material |
| `mask-image` | Done. `SaveLayerAsMaskImage` resolves the mask into its own layer and the composite multiplies by it |
| `backdrop-filter` other than blur | Accepted and ignored. The backdrop belongs to Slate, and the only thing Slate will do to it in place is blur |
| `decorator: shader(...)` | Done. additive, scanlines and sweep, rasterised rather than shaded |

### box-shadow, and why the layer path draws with RHI rather than FCanvas

**An outer box-shadow must not paint inside its own element**, and RmlUi says so with `ClipMaskOperation::SetInverse`: "everywhere except this shape". A clipping rectangle cannot express a complement, and `FCanvas` exposes no stencil state, so the layer path draws with RHI: recorded on the game thread as RmlUi issues the calls, replayed on the render thread in one pass per layer. Recording lets the stencil live for a whole layer, which a per-draw transient graph cannot.

Without it, a panel measured `(20,20,26)` where `#1f1f28` was expected: `31 x (1 - 89/255) = 20.2`, the surface darkened by exactly the shadow's own alpha because the shadow painted over its own interior.

Three real bugs had to be fixed along the way and none moved that number: gamma, a `CompositeLayers` that popped and re-pushed (which clears the layer it is about to composite), and straight-alpha blending against premultiplied data. **A fix that changes nothing is evidence: measure rather than guess again.** Reading the layer's pixels back took one build and settled it.

Gamma: the target is sRGB and the shader's output is encoded on write, so vertex colours are decoded going in. `pow(2.2)` is not close enough: it lands `#1f1f28` on `(22,22,34)`, because the real sRGB curve has a linear toe and UI colours are dark, exactly where the two disagree. `FLinearColor::FromSRGBColor` gives `(28,28,38)`. The remaining 3/255 is the 8-bit round trip through the target.

## Rendering

- **Colors and textures are premultiplied alpha** in RmlUi 6.x (straight alpha in 5.x). Blend func must be `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`.
- **The 6.x render interface names are `CompileGeometry` / `RenderGeometry` / `ReleaseGeometry`.** 5.x's `RenderCompiledGeometry` / `ReleaseCompiledGeometry` are gone, and `CompileGeometry` is a required pure virtual.
- **Compiled geometry vertex and index data stay valid until `ReleaseGeometry`**, so a render interface may retain pointers into it instead of copying.
- `SetScissorRegion` uses window coordinates with a **top-left origin** and is unaffected by `SetTransform`.

## There is no user-agent stylesheet, and documents shrink to fit

Two layout defaults that make a first document look broken when the markup is fine.

Folio ships `rcss/folio.rcss` covering both, reachable as `/folio/folio.rcss` from any content root. It counts as engine integration rather than game UI: it establishes what elements mean, not what they look like.

**Every element defaults to `display: inline`.** RmlUi ships no user-agent stylesheet, so `div`, `p` and `h1` are inline until something says otherwise, and a document without a base stylesheet renders as one run of overlapping text. `Samples/assets/rml.rcss` in the RmlUi tree is sample content, not part of the library. A base stylesheet establishing block display is mandatory.

**An unstyled scrollbar eats the entire content width.** Scrollbar elements have no default size, and `width: auto` on a `scrollbarvertical` resolves to the container's full content width. A container styled `overflow: auto` lays out correctly until its content overflows; then the scrollbar appears, takes all the width, and every child collapses to its own padding and border. Text inside a child with `overflow: hidden` disappears entirely. Measured: rows in an 852px list dropped to 32px when a fourth row pushed them past the container height, and back to 842px once `scrollbarvertical { width: 10dp }` existed. It reads as a flex bug and is not: a `display: block` container fails identically.

`folio.rcss` therefore sets scrollbar geometry as well as block display. Geometry only; the look belongs to the game.

**A document is sized shrink-to-fit, and `display: block` does not change that.** The document element is laid out against the context as if floating, so its width collapses to the widest line of content: an 800px context rendered a 681px document box, the longest text plus padding. Set `width: 100%` (and `height: 100%` for a full-viewport background) on the document element explicitly.

## Document lifecycle

- **`ElementDocument::Close()` is deferred.** The document is not destroyed until the next `Context::Update()`. Reloading without an `Update()` between close and load leaves the old document alive.
- **`ReloadStyleSheet()` preserves the DOM, focus, scroll and data-model state** and clears the relevant caches itself. It does **not** reapply inline `style=""` attributes, one reason inline styles are banned in the conventions.
- **`Element::Focus(bool focus_visible = false)` defaults to false.** Restoring focus after a reload without passing the captured value drops the focus ring and looks like a `:focus-visible` bug.
- **Initial focus is not automatic.** A freshly shown document has no focused element until something calls `Focus()`.

## Diagnostics

**Almost every RCSS problem is logged at `LT_WARNING`, not `LT_ERROR`**: unknown properties, invalid decorator types, bad spritesheet resolution, duplicate `@decorator` names, missing textures. `LT_ERROR` is essentially `Log::ParseError`, RML/XML structural failure. A check that only looks at errors passes a thoroughly broken stylesheet. That is what `--strict` is for.

`SystemInterface::LogMessage` returning `false` **interrupts RmlUi execution**. Only do that for `LT_ASSERT`.

## Unreal builds without RTTI, and RmlUi uses `dynamic_cast`

**Build RmlUi with `RMLUI_CUSTOM_RTTI`, and define it in the consuming module too.**

Unreal compiles with `/GR-`. RmlUi's `rmlui_dynamic_cast` is a plain `dynamic_cast` unless `RMLUI_CUSTOM_RTTI` is set, in which case it uses RmlUi's own type identifiers. Building the library with `/GR-` and without that option compiles and links cleanly, then dies at the first cast, `Context.cpp:70` inside `Rml::CreateContext`: the editor crashes the moment a context is created, with no message and no usable callstack.

`RMLUI_CUSTOM_RTTI` is a **PUBLIC** compile definition in RmlUi's CMake, so the header picks a different `rmlui_dynamic_cast` depending on it. The library and every consumer must agree, or it is an ODR violation that crashes the same way. It is set in `cmake/ue/CMakeLists.txt` and again in `RmlUiLibrary.Build.cs`; change both or neither.

## Slate ships its own msdfgen, and it does not collide with ours

Unreal 5.8 has an amalgamated msdfgen at `Engine/Source/ThirdParty/msdfgen/`, compiled into SlateCore for its own SDF text path (`ESlateShader::SdfFont`, `MsdfFont`). Folio builds a separate copy, so in a monolithic packaged build both land in one executable, normally a `LNK2005`.

It is not one, because `SlateSdfGenerator.cpp:34` does `#define MSDFGEN_PARENT_NAMESPACE SlateMsdfgen` before including `msdfgen.cpp`. Verified in the mangled names:

```
Slate   generateMTSDF@msdfgen@SlateMsdfgen@@
Folio   generateMTSDF@msdfgen@@
```

`Module.SlateCore.2.cpp.obj` contains no unnamespaced `@msdfgen@@` symbol. So Folio needs no parent namespace of its own and no patch.

**Re-check this on an engine upgrade.** If Slate drops that define, the two copies merge into one namespace, and the failure is asymmetric: editor builds link SlateCore as a DLL and keep working, while the packaged monolithic build fails. The fix is a `patches/msdfgen/*.patch` adding the same macro on our side, which is what `folio_patch_command()` exists for.

Two other facts about that amalgam. It sets `MSDFGEN_DISABLE_VARIABLE_FONTS` and `MSDFGEN_DISABLE_SVG` by default, and compiles under `/GR-` with exceptions off, which shows msdfgen needs neither RTTI nor exceptions, so `folio_set_msdfgen_options()` does not fight Unreal's flags. Its Skia is enabled only `WITH_EDITOR`, and without it `resolveShapeGeometry` is not compiled at all (the whole file is inside `#ifdef MSDFGEN_USE_SKIA`). Folio builds it unconditionally, from the amalgam vendored in `Source/ThirdParty/SkiaSimplify/`, because `MsdfGlyphGenerator` calls `resolveShapeGeometry` to union a glyph's overlapping contours. Left to `normalize()` and `orientContours()` alone, a heavy weight reconstructs a solid stem as two thin strokes.

## HarfBuzz is the engine's copy, and that decides three other things

HarfBuzz is a C library with no namespace to hide in, so two builds of it in one monolithic executable is a duplicate symbol for every entry point. Folio therefore links the prebuilt `Engine/Source/ThirdParty/HarfBuzz/harfbuzz-2.4.0`, with three consequences:

- **ICU comes with it.** That build takes its Unicode character properties from ICU, so `FolioCore.Build.cs` has to name ICU as well or the library does not link.
- **The allocator has to be supplied.** It is compiled with `hb_malloc_impl` and friends redirected to `HarfBuzzMalloc`/`Calloc`/`Realloc`/`Free`, which Slate defines in `FontCacheHarfBuzz.cpp` **without exporting**. A second module linking the same static library gets an unresolved external, so `FolioCoreModule.cpp` defines its own four, under `!IS_MONOLITHIC`, because a monolithic build has one copy of the library and takes Slate's.
- **`bCompileFreeType` and `bCompileICU` gate it.** `HarfBuzz.Build.cs` returns an unusable module when either is off, and only routes to 2.4.0 on Windows, Mac, iOS, Android and Unix. `FolioCore.Build.cs` repeats the same condition and sets `FOLIO_WITH_HARFBUZZ` from it. A build without it falls back to one glyph per codepoint, as RmlUi's own engine does.

The preview tool cannot link the engine's library, so `folio_add_harfbuzz()` builds the same release from source. Its file list mirrors Unreal's `BuildForUE` script with two differences: `hb-ft` is left out, since nothing calls it and HarfBuzz then needs no FreeType; and character properties come from the UCDN tables HarfBuzz bundles rather than ICU. The two disagree only about codepoints assigned after their respective Unicode versions.

It compiles as **C++17 while everything else here is C++20**. HarfBuzz 2.4.0 compares two objects of one type with a member `operator==` taking a non-const reference, and C++20's reversed candidates make that ambiguous. Its public interface is C, so the standard does not cross the boundary.

## Texture formats

**RmlUi's own texture loader reads uncompressed TGA and nothing else.** Not PNG. Every real atlas failed with `Only 24/32bit uncompressed TGAs are supported`, which reads as a corrupt file rather than an unsupported format. `uipreview`'s render interface decodes `.png` itself through lodepng, which it already links for screenshots, and premultiplies on the way in because 6.x expects premultiplied texture data and lodepng decodes straight alpha.

**A distance field atlas is not colour.** `SRGB = false`, and do not premultiply. Both mistakes are quiet:

- sRGB on an atlas applies a transfer curve to numbers that are distances, so the field's zero crossing moves and every glyph comes out the wrong weight.
- Premultiplying multiplies RGB by alpha. In an MTSDF, alpha is the *true signed distance*, not coverage. Premultiplying the vector atlas eroded the art to blobs.

The renderer detects a field atlas *before* the premultiply step for this reason. Bilinear, clamped, no mips, never streamed.

## Missing sprites

Most decorators resolve a value as a sprite name first and **silently** fall back to treating it as a filename. Nothing is logged at the decorator layer; the only signal is `TextureDatabase`'s `"Could not load texture"` warning when `RenderInterface::LoadTexture` returns 0.

So `LoadTexture` never returns 0: on failure it returns a generated magenta checkerboard, which **does** suppress RmlUi's own message, so the wording is ours. `LoadTexture` runs **before layout**, so it cannot know the sprite's drawn size; that is captured from the compiled geometry's bounding box at draw time.

**The name arriving at `LoadTexture` is already joined against the document directory.** An undeclared sprite `icon_sword` referenced from `/UI/screens/hud.rml` arrives as `/UI/screens/icon_sword`. So "contains a path separator, therefore a file reference" is wrong: every unresolved sprite looks like that. Classify on the **file extension**: none means a sprite name, one means a file or spritesheet `src` reference.

**Not every decorator reaches `LoadTexture`.** Confirmed by experiment:

| Decorator | Undeclared sprite behaviour |
|---|---|
| `image`, `tiled-horizontal`, `tiled-vertical`, `tiled-box` | Silent. Falls through to `LoadTexture`, caught by the placeholder. |
| `ninepatch` | Sprite-only. Never reaches `LoadTexture`, logs `Could not find sprite named '<name>' in ninepatch decorator.` plus `Decorator 'ninepatch' ... could not be instanced, declared at <file>:<line>`, and renders nothing at all. |
| `<img sprite="...">` | Not a decorator, and sprite-only in the same way. Logs `Could not find sprite '<name>' specified in img element <path>` and draws a white box at the element's size. |

The `img` wording differs from the ninepatch one (`sprite '<name>'` against `sprite named '<name>'`), so match both prefixes when scraping the log. Neither route reaches the render interface, so neither has a drawn size; for `img` the size is recovered from the element's laid-out box after the document settles, which is why an `img` entry in `ASSETS_WANTED.md` has a real size and a ninepatch entry is `0x0`.

So missing sprites are collected from two sources: the `LoadTexture` fallback, and the ninepatch warning parsed out of `LogMessage`. The ninepatch route supplies a file and line number.

## Data models built from JSON

`Rml::VariableDefinition` supports a fully dynamic model with no compile-time structs, and `data-for` over an array of objects works. `BindCustomDataVariable` takes a `DataVariable` directly, so `RegisterCustomDataVariableDefinition` is not needed as long as the caller keeps the definition objects alive.

**Every bound address needs a pointer that stays valid, not only the root.** RmlUi re-walks an address from its *bound* variable on each update, so binding top-level keys as pointers into one root JSON object and then reassigning that root leaves each bound pointer dangling: nlohmann frees the sub-objects on assignment. The symptom is the whole model going blank after a reload, with `struct has no member '<name>'` warnings for nested paths.

Each top-level key therefore gets its own heap slot (`map<string, unique_ptr<ordered_json>>`) and a reload assigns through the slot. Everything below a bound slot is re-resolved per update, so nested objects and arrays may change shape freely, including arrays changing length.

**Keys that appear only after the model is constructed cannot be bound**, since the constructor is gone. A reload that introduces a new top-level key needs a document rebuild.

`VariableDefinition::ReflectMemberNames()` takes no node pointer, so member names cannot be reflected per node. The debugger's data-model viewer will not list JSON object members.

## Screenshots from a hidden window

`glReadPixels` against a window created with `GLFW_VISIBLE=false` produces a capture **byte-identical** to the same scene in a visible window (0 of 1,228,800 bytes differing). No FBO indirection is needed.

Capture after `RenderInterface_GL3::EndFrame` and before `glfwSwapBuffers`: `EndFrame` resolves the renderer's layer framebuffers into the default framebuffer.

## Git LFS is not set up

`.gitattributes` marks `*.png` / `*.ttf` / `*.otf` as plain `binary`, not LFS, because `git lfs install` has never been run on this machine and an undefined filter is worse than no filter. Folio bundles no art or fonts, so nothing needs it yet. Before a repo starts accumulating atlases, run `git lfs install` and switch those lines to `filter=lfs diff=lfs merge=lfs -text`.

## Profiling

Folio reports to Unreal Insights, `stat folio`, and CSV captures. Capture with:

```
UnrealEditor.exe <project> -trace=default,counters,folio -statnamedevents
```

`folio` is its own channel so a capture can ask for it without everything else. In the editor, `stat folio` shows the same timings live.

The scopes, in the order they cost:

| Scope | What it covers |
| --- | --- |
| `Folio/GenerateGlyph` | One glyph's field, cold. Everything below is inside it. |
| `Folio/GenerateField` | msdfgen itself. **This is the expense**, around 2.8 ms a glyph. |
| `Folio/ResolveOverlaps` | Skia unioning the outline. Around 0.04 ms, 1.5% of the glyph. |
| `Folio/ReadGlyphCache` | Reading an instance's fields back from disk. |
| `Folio/PackGlyph`, `Folio/UploadGlyph` | Placing it in the atlas and sending it to the GPU. |
| `Folio/GenerateString`, `Folio/GetStringWidth` | What RmlUi calls per text element. Cold, these contain the generation above. |
| `Folio_Render`, `Folio_Update` | The canvas widget's paint and tick. |
| `Folio_CompileGeometry`, `Folio_DrawGeometry` | Per draw, on the game thread. |
| `Folio_ResolveLayers` | The layer stack's replay, on the render thread. |

Counters: `Folio/AtlasPages`, `Folio/AtlasMemory`, `Folio/AtlasGlyphs`, `Folio/FaceInstances`, `Folio/GlyphsGenerated`, `Folio/GlyphsFromCache`. Memory is tagged for `-llm` under `Folio`.

**The generation cost is msdfgen's, not Skia's**; resolving overlaps is a rounding error beside it. An earlier commit message says otherwise and is wrong.

FolioCore cannot use Unreal's profiling macros, since it compiles into the preview tool too, so it names scopes through a hook table in `FolioCore/FolioTrace.h` that the Unreal module forwards to Insights. The preview tool implements the same hooks and totals them:

```
uipreview <doc> --font-report
```

Numbers there are inclusive, so a parent scope contains its children.

## Toolchain

**The MSVC toolset must match whatever UnrealBuildTool picks; do not guess it.** On this machine UE 5.8's UBT uses **14.50.35717**, from a Visual Studio 18 BuildTools install, not 14.38.33130. RmlUi built with 14.38 linked into modules built with 14.50 without complaint, which is the danger: RmlUi's interface passes `std::string` and `std::vector` across that boundary, and a layout change corrupts rather than fails.

`build_ue_libs.sh` defaults to `--toolset unreal`, which reads the engine's `Config/Windows/Windows_SDK.json` and picks the toolset UBT would: banned and too-old versions out, earliest preferred family first, highest version within it. Picking the newest installed toolset instead links an older-STL Unreal module against a newer-STL `rmlui.lib` and fails with unresolved `__std_rotate`, `__std_find_not_ch_1` and similar. A `CompilerVersion` set in `BuildConfiguration.xml` is not read, so UBT's `Toolchain Information` line is the authority; pass `--toolset` when they disagree. `build.sh` builds the standalone tools and keeps `latest`.

`CMakePresets.json` names no version: its toolset strategy is `external`, so CMake would never apply one.

A build tree caches `CMAKE_CXX_COMPILER` from its first configure, but `INCLUDE` comes from whatever `vsenv.sh` resolves now. When those drift apart the STL fails the compiler out:

```
yvals_core.h(929): error C2338: static_assert failed:
    'error STL1001: Unexpected compiler version, expected MSVC 19.44 or newer.'
```

followed by a hundred errors inside `<type_traits>` and `<xutility>`. It looks like the dependency that compiled first is broken. It is not: run `build.sh --clean`.

`scripts/build_ue_libs.sh` builds RmlUi for Unreal, separately from the preview tool, with different settings: the engine's FreeType rather than our own, `/MD`, no RTTI, and output staged into `Source/ThirdParty/RmlUiLibrary`. Those libraries are **not committed** (tens of megabytes, re-entering history on every rebuild), so a fresh clone runs the script once before building the plugin. `RmlUiLibrary.Build.cs` fails with that instruction rather than a link error if they are missing, and also when they were built with different patches than `patches/RmlUi/` holds: the script records each patch's hash beside the libraries as `RMLUI_PATCHES`, so a pull that adds or changes a patch asks for the rebuild instead of failing to compile.

**The engine's FreeType needs zlib and libPNG named explicitly.** `FreeType2.Build.cs` declares them as private dependencies, which do not reach the module that links, so `freetype.lib` arrives with 27 unresolved `png_*` and `inflate*` symbols. Add `zlib` and `UElibPNG` alongside `FreeType2`.

**`RMLUI_FONT_ENGINE` is `none` in both builds.** Folio supplies its own `Rml::FontEngineInterface`, so RmlUi's FreeType engine is not compiled at all, which also avoids colliding with the engine's FreeType (`Engine/Source/ThirdParty/FreeType2`).

So `Rml::Initialise()` **fails hard** with "No font engine interface set!" unless `SetFontEngineInterface` ran first (`Core.cpp:109-118`). `FolioModule::StartupModule` and `uipreview`'s `App` both install theirs before initialising; a new host that forgets gets a loud failure, not a subtle degradation.

The debugger's font loads through the **memory** `LoadFontFace` overload under the family `rmlui-debugger-font`. A font engine that only implements the file overload gives a debugger with no text at all, which reads as a rendering bug.

## Live Coding cannot introduce a global shader type

Hot-patching in a new global shader (`FFolioUIMsdfPS`) kills the editor:

```
Assertion failed: !AreShaderTypesInitialized() [Shader.cpp:315]
Shader type was loaded too late, use ELoadingPhase::PostConfigInit on your module
```

The patch DLL links and the process dies while loading it, so the assert is all there is. Global shader types are registered from static initialisers and the engine seals that list long before a patch arrives; the message's advice does not apply, because the module already loads early enough on a normal start.

Editing the body of an existing shader is fine: the editor picks up a changed `.usf` on its own. Adding, removing or renaming an `IMPLEMENT_GLOBAL_SHADER` needs the editor closed and a full build.

## A fullscreen RDG pass does not use your vertex shader

Every layer effect in Unreal (outer box-shadow, `filter`, `backdrop-filter`) rendered as nothing, because of one line in `FolioBlur.usf`:

```hlsl
float4 MainPS(float4 SvPosition : SV_POSITION, float2 UV : TEXCOORD0)
```

`FPixelShaderUtils::AddFullscreenPass` supplies **the engine's** vertex shader, not the `MainVS` beside that function. Nothing writes `TEXCOORD0`, so every tap sampled the same undefined coordinate, the blur returned a uniform value, and the pass overwrote the layer with it. UV comes from `SvPosition.xy * InvSize` instead (`InvSize` was set and never read, which was the clue).

The symptom points at the layer, not the filter, since the blur runs on the layer's result. And it is silent everywhere: no RHI validation error, no shader warning, correct pipeline state and geometry, and a dead vertex shader that still compiles and still gets an `IMPLEMENT_GLOBAL_SHADER`.

**RenderDoc settled it, by reading the render target between passes.** After `FolioLayer` the target held 15188 non-zero pixels; after `FolioBlur` it held zero. PSOs, scissors, volatile buffers and RDG culling were all checked and all fine.

Capturing: the Folio window is its own `SWindow` with its own swapchain, so `RenderDoc.CaptureFrame` (which needs `-AttachRenderDoc` on the command line) captures the main editor viewport, not the document. Attach from the RenderDoc UI with **capture all activity** and make the window repaint. The pass is marked `FolioLayer` in the event browser; confirm the right capture by looking for a render target the size of the document window.

## Folio bundles typefaces

Folio ships Inter and RobotoMono, both variable, under `fonts/`, rather than defaulting to Unreal's Roboto.

The engine's Roboto is a static face. Variable fonts are most of the value: `font-weight: 450` is a real instance rather than a snap to the nearest cut, and `opsz` is driven from the requested size automatically. Shipping faces with axes makes that reachable by default rather than only after a game supplies its own.

The same rule holds in both renderers: the game's own `UI/fonts/` if it has any faces, else the faces Folio ships, else the engine's Roboto. Only the first of those that has faces is loaded, so a game with its own fonts never carries Folio's.

The first face registered becomes the fallback. Without one, a document naming an unregistered family renders nothing at all, which reads as a layout bug.

## The font engine must be released before the render interface

Symptom: `uipreview` segfaulted roughly one cold-cache run in thirty, always after the screenshot was on disk, never in the same document twice. `App::~App` shut RmlUi down, then called `Backend::Shutdown()`, which destroys the render interface. The font engine is a member, so it was destroyed *after* the destructor body, and `FMsdfAtlas::~FMsdfAtlas` releases every atlas page through `IMsdfTextureSink`, which is that render interface. A use-after-free on the sink's vtable on every run, usually surviving because freed memory still held its old contents.

`font_engine.reset()` runs in the destructor body, after `Rml::Shutdown` (which still reads through the engine while releasing font resources) and before the backend goes.

The general rule: **anything holding an `IMsdfTextureSink` has to die before the object implementing it.** Member declaration order will not arrange that when part of the teardown is explicit calls in a destructor body.

**Use `build.sh --asan` for any intermittent crash here.** It named this one on the first run, with both the free and the later read. It builds into `build/win64-asan` but writes its binaries to the same `bin/win64`, so rebuild without the flag afterwards, and run it from a shell with the MSVC environment imported or the sanitizer runtime will not resolve.

## Advances changed when the text engine did

Folio's advances come from FreeType's unhinted `linearHoriAdvance`, scaled linearly from design units. Hinted advances do not scale linearly, and a non-linear advance cannot serve one size-independent atlas, so this is deliberate.

So every screen re-flows slightly against a hinted rasteriser, and a line break is not stable across dp ratios: an advance is a whole number of pixels at each size, so doubling the ratio halves the relative rounding error and words move between lines.

`uipreview --metrics-report` and `scripts/verify.sh --baseline` make that visible. Compare like for like: same size, same dp ratio. A break that moves after an engine change is usually correct and still has to be looked at.

## Baking is a build step, not a runtime one

`msdfbake` runs offline and writes into the game's content:

- `msdfbake font` writes `.folioatlas` caches. They are an optimisation: a missing one is generated at runtime, about 0.2-0.5 ms per glyph, so packaging without them works and stutters on first use of a face.
- `msdfbake vector` writes the atlas PNG, its sidecar and a `@spritesheet` block. These are **not** optional. Nothing generates a vector atlas at runtime, so an unbaked SVG is a missing sprite in the packaged build.

So the vector bake belongs in the content pipeline ahead of cook, next to whatever packs the raster atlases, and its outputs are committed like any other art. Re-run it after editing an SVG. RmlUi caches textures by path, so a re-baked atlas at the same path renders stale in the editor until it restarts.

## Engine upgrade checklist

Things that read engine internals and would fail quietly if the engine moved:

- **`ChooseMaterialShaderTypes`**: the UI-domain material reaches the distance field shader because `ElementBatcher.cpp` tags custom-verts batches `ESlateShader::Custom` and `SlateRHIRenderingPolicy.cpp` maps that to `TSlateMaterialShaderPS<ESlateShader::Custom>`. If that mapping changes, text loses its shader and draws the raw field as colour.
- **`FSlateResourceHandle`'s private constructor**, friended only to `FSlateShaderResourceManager`. Only `GetResourceHandle(Brush)` yields one; anything else does not compile, and the usual workaround is a reinterpret cast.
- **`FSlateBaseUTextureResource::AccessRHIResource`** calls `FApp::GetCurrentTime()` from the render thread, which ensures on `IsInGameThread()`. Folio uses `FSlateDynamicImageBrush::CreateWithImageData` to get a native `FSlateDynamicTextureResource` and avoid that branch.
- **Slate's own MTSDF maths** in `SlateElementPixelShader.usf`. Folio's spread term is taken from it; if Slate's changes, the two stop agreeing about what a field means.
- **msdfgen is vendored by SlateCore too.** Both copies link into a monolithic build. Slate wraps its copy in `MSDFGEN_PARENT_NAMESPACE` (`SlateMsdfgen`) and Folio's has none. Check that Slate still defines it: without it the editor links and the packaged build fails with `LNK2005`.
- **HarfBuzz's allocator names and its version routing.** Folio defines `HarfBuzzMalloc` and friends for modular builds and skips them for monolithic ones, assuming Slate still defines them and still does not export them. If the engine moves to the 12.2.0 copy that also sits in that directory, check whether the allocator redirect and the ICU dependency survived, and pin the preview tool to the same release.

## Releasing a texture can delete a live glyph atlas

`Rml::ReleaseTextures()` releases **callback** textures as well as file ones (`TextureDatabase.cpp`), and a distance-field page is handed to RmlUi as a callback texture. So a hot reload, which calls it to pick up changed art, arrives at `ReleaseTexture` holding the font engine's own atlas page.

Freeing it there deletes a texture the font engine still owns and writes new glyphs into. RmlUi then re-runs the callback and gets the same handle back, now naming nothing. In the preview tool every glyph disappeared on the next `.rcss` edit; the screenshot went from 82 KB to 3.4 KB.

Both renderers track which field pages came from a file, and release only those. A glyph atlas belongs to the font engine and goes at shutdown through `ReleaseAtlasHandle`.

`uipreview --reload-textures` renders, releases everything, renders again and captures. `verify.sh` compares that against an ordinary capture and fails on any difference, the only cheap way to catch this class of bug, since it needs a reload and reloads normally only happen by hand.

## A glyph atlas has to be written twice

`UpdateTextureRegions` writes the RHI texture and only the RHI texture. The `UTexture2D`'s own mip stays as created, zeroed, and anything that reinitialises the resource rebuilds the GPU texture from that mip.

The editor does that when it switches to a mobile preview platform: `SetPreviewPlatform` applies a preview device profile, and the atlas comes back blank. Every glyph disappears while boxes and sprites keep drawing, because those textures had their pixels written into the mip before `UpdateResource`. It reads as "text does not work on mobile" and has nothing to do with mobile; a device profile change does the same on any platform.

So `UploadAtlasRegion` writes each region into `Mips[0]` as well as uploading it. The mip is already allocated by `CreateTransient`, so the mirror costs only the copy.

## An effect cannot reach as far as the field does

Symptom: `outline(20px)` on 40dp text drew each glyph as a solid rectangle of colour, quad edges and all. It looks like effect quads leaking into each other; they are not.

The outline branch is `saturate((sd - (Threshold - Offset)) * Spread + 0.5)`. Clamping the offset to exactly half the field's range put `Threshold - Offset` at zero, which is where the field saturates. Every texel beyond `PxRange/2` holds zero, whether one texel past the edge or a whole em away, so every one evaluated as sitting on the outline and came out at half coverage. The whole padded quad filled, and the quads abut.

The cap is `(PxRange/2 - 1) / PxPerEm`, 0.104em at the defaults instead of 0.125em. One texel of field is held back for the effect's own edge to fade out in. `MaxEffectEm()` is therefore not half the range, and the em-to-field-units conversion does not derive the range from it: they are separate numbers.

The glow and blur branch's lower bound is clamped to zero for a different reason, so it is safe from this, but a glow with no blur gives `smoothstep` a zero-width ramp, a division by zero. Its upper bound has a floor.

`Tools/uipreview/tests/effect_edges.rml` asks for every effect well past the cap. It is expected to warn and to exit 5 under `--strict`; it must never show a rectangle or a seam between adjacent glyphs.

## A layer stack has no height of its own

Every sprite in a `--layers` bake is `position: absolute`, so none contributes to the container's height. Give the container only a width and nothing renders, which reads as a broken bake rather than a collapsed box.

RCSS has no `aspect-ratio` and no `calc()`, so no stylesheet can turn a width into the right height. The only thing in RmlUi that carries a ratio is an image element's intrinsic size.

So the baker emits a `<name>_frame` sprite per drawing, holding the drawing's proportions, and a `.<name>-frame` rule that is `visibility: hidden` (it keeps its box and never draws). A hidden `<img>` on it is the first child of the stack, and the width alone is then enough.

The frame rectangle is **not packed and owns no atlas area**. A sprite rectangle is only coordinates and nothing samples this one, so it may overlap anything. It is written as `0px 0px W H` scaled to a longest side of 512, which keeps the ratio error under a fifth of a percent and cannot exceed a page.

`Tools/uipreview/tests/vector_stack.rml` covers it, with the container behind the art in magenta: the drawing's own background layer must cover it exactly at every size.

## backdrop-filter is a pair, and the second half is the one that matters

`outside-blur` inverts a backdrop filter: the element becomes a hole and everything else is blurred, as a modal scrim wants.

RmlUi issues a backdrop filter as two composites. The first reads what is on screen, runs the filters, and writes the result into a scratch layer. The second puts that layer back, clipped to the element. Inverting the first achieves nothing (it only chooses which pixels get blurred, and the second still stamps the result inside the element). The inversion belongs to the second, which both renderers already defer to.

The first pass still changes: it is clipped to the element, so the scratch layer only holds blurred pixels there, and the bands the second pass draws, all outside it, would read unwritten pixels. So the first pass runs unscissored.

Four bands (above, below, left, right), because a scissor is a rectangle and one cannot be cut out of another. Each composites the whole layer rather than a slice, so every band reads the same pixels and they meet without a seam. So the hole is a rectangle: a rounded dialog cuts a square one.

In Unreal it is four `MakePostProcessBlur` elements instead of one, and the clip has to come off the stack first: it is the element's own scissor, and every band is outside it.

The filter is registered through `Factory::RegisterFilterInstancer` from `FolioFilters.h`, which is header-inline for the same reason the font effect instancers are: an instancer's constructor calls `RegisterProperty`, which resolves parsers through the `StyleSheetSpecification` that only one module's `Rml::Initialise` populated.

## Stroking by piling up pieces does not survive a real drawing

A stroke has to become area, because a field has no notion of a line. A rectangle per segment, a disc per vertex, and Skia's path operations to union the pile is correct on everything simple and fails on real drawings.

A ragged path (64 points, ~169 after flattening, turning sharply every few units) produced hundreds of overlapping contours, and the union came out **solid**: a `fill:none` outline baked as a filled silhouette and hid the shape it was drawn over. Stroke width, winding consistency and a closed stroked square all checked out fine, so none of those is the cause.

The stroker walks each contour once and emits one offset polyline per side, with the joins put in: miter with its limit, round, or bevel, read from the SVG. A closed path gives two loops wound against each other, a ring under non-zero fill. An open path gives one loop (up one side, round the cap, back down the other) with butt, round or square caps. Two contours instead of hundreds, and a faster bake.

On the inside of a corner the two offsets cross. The path operations resolve that self-intersection; it is not special-cased.

Cap arcs taken the short way pass through the stroke and bite a crescent out of its end. The direction has to be the one that goes through the end point plus the outward tangent.

`Tools/uipreview/tests/vector_strokes.rml` is one of each case, at two sizes.

## Font effects all drew behind the glyph, whatever they asked for

`Rml::FontEffect` lets an effect choose a side with `GetLayer()`, Back or Front. Every effect RmlUi ships picks Back, as did all four of ours, so `GenerateString` drawing the whole effect set before the glyph went unnoticed.

`fill-image` is a front layer by construction (the glyph drawn again at the same coverage with a pattern multiplied in), and the base pass painted straight over it. The symptom was a red fringe: only where the glyph's own antialiasing let it through did the front layer survive.

Back layers, then the glyph, then front layers.

## Textured text needs a second sampler, and that means editing the material

The pattern is a second texture multiplied into the resolved coverage. `FolioMsdfShade` has `Fill`, `FillSampler`, `FillUV` and `FillStrength`, so `M_FolioMSDF`'s Custom node needs three matching inputs, and it is a binary asset that a text diff cannot edit.

Done headlessly, and repeatable:

```
UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<file> -unattended -nosplash
```

with `PythonScriptPlugin` temporarily added to the project's plugin list. It does not belong in `Folio.uplugin`: it is a one-off authoring tool, not something every game on Folio should have to enable. The script is idempotent: it checks for the `Fill` input and exits if it is already there.

`print` from a commandlet only reaches the console with `-stdout -FullStdOutLogOutput`. Without those the script runs silently and looks like it did nothing.

**Where the pattern is read from is the host's job, not the field maths.** Unreal computes the UV from `Parameters.SvPosition` in the Custom node; the preview tool from `gl_FragCoord`, flipped, since GL puts the origin at the bottom. Both are screen space, so a pattern is continuous across a whole run instead of restarting on each glyph quad. `check_shader_parity.sh` skips a block fenced with `FOLIO_MSDF_HOST_BEGIN`/`END` for this: the two renderers are expected to differ there, and only there.

## RmlUi's visual test suite, and what its numbers mean

`scripts/visual_tests.sh` runs the 109 documents in `Tests/Data/VisualTests` from the fetched RmlUi tree through uipreview. They cover layout, floats, flexbox, tables, transforms, filters, clip masks, box-shadow and text, and exercise the render interface far harder than anything hand-written here. `verify.sh --visual-tests` runs them; it is off by default because they take longer than the rest of the harness.

All 109 render. The suite still reports 197 errors and 448 warnings, almost none of them Folio's:

- **180 errors across 89 documents are `<link>` tags with no `type`.** The suite uses them for metadata (a link to the W3C test it came from, a reference image). RmlUi's own parser rejects them; their harness ignores the log.
- **17 documents link `../Tests/Data/style.rcss`**, which is wrong relative to the document wherever it is resolved from. Those seventeen render unstyled, which accounts for 430 of the 448 warnings: every element then has no `font-family`.
- One warning is Folio's, and correct: a test asks for a font effect wider than the field describes and is told it was clamped.

So the bar is not zero. The baseline in `Tools/uipreview/tests/visual_baseline.json` records what each document does today, and the script fails on any **increase**. A renderer regression moves those numbers; an unhandled document stays where it is and is visible in the file.

The script fixes up two things in the build tree, because uipreview has one content root by design and their harness has several search paths: it copies `Samples/assets/*.tga` under `Tests/Data/assets/` so the image tests resolve their art, and it registers the four LatoLatin faces the suite's stylesheet names. A clean rebuild undoes both and the script redoes them.

## The Phase 3 gate, and how to re-run it

`folio.Gate [document] [seconds]` is the gate as a command. It puts a plain Slate widget in the viewport at order 50, the document at 100, and another widget at 200 (the orders game UI uses), waits for the game to be up, then screenshots.

```
UnrealEditor.exe <project>.uproject -game -windowed -resx=1280 -resy=720 -nosplash \
    -ExecCmds="folio.Gate /folio/recipes/pause.rml 6"
```

The image lands in `Saved/Screenshots/WindowsEditor/FolioGate.png`. Passing means the document renders, the order-200 widget covers its top edge, and the order-50 widget is hidden behind it.

**The command has to poll.** `-ExecCmds` runs during engine init, before there is a game viewport, and a level load clears viewport widgets anyway, so a command that acts immediately finds nothing and logs nothing (or screenshots the loading screen). It waits on `GetWorld()->HasBegunPlay()` and then gives the document a few seconds to load its stylesheet, generate its glyphs and draw.

## Decorators paint in reverse source order

`decorator: linear-gradient(...), shader("additive #ff5028")` draws nothing visible. Swap the two and the effect appears. Comma separated decorators are painted **last to first**, so the one named first ends up on top, as CSS `background-image` layers stack, and the opposite of the natural reading.

"An effect listed after the background does nothing" looks exactly like "the effect is not implemented". `UI_CONVENTIONS.md` documents the correct order.

## The shader decorator, and why nothing is shaded

`decorator: shader("...")` is RmlUi's seam for a backend's own fragment shader: the string is passed untouched to `CompileShader`. Slate has no equivalent seam through `MakeCustomVerts`, and none of the effects wanted here needs one: each is a pure function of position over the element's box. So they are evaluated once into a small texture, in FolioCore, and both renderers draw the same picture from the same code. A pair of hand-written shaders would drift.

**Additive blending needs no blend state.** RmlUi composites premultiplied, `src + dst * (1 - a)`, so a colour written with zero alpha adds to what is underneath. One line in the rasteriser.

The trade is a fixed set: `additive`, `scanlines`, `sweep`. An unknown name warns once and draws nothing. Adding another is a function in `FolioShaderEffects.cpp` and nothing else: no shader, no material, no per-renderer work.

## clip-path, honoured rather than dropped

A clip is an intersection and a field carries one silhouette, but `clip-path` is still expressible in the bake: Skia's path operations are already vendored for unioning a glyph's overlapping contours, and an intersection is the same call with a different operator.

The obstacle is nanosvg, which does two things:

- It **discards the relationship**. So the baker reads the SVG itself for `<clipPath id="A">` and `<g clip-path="url(#A)">`, and maps shape ids to the clip that applies to them. Only the relationship: the geometry comes back from nanosvg with every transform already applied.
- It **emits the clip path's own geometry as an ordinary visible shape**. SVG never renders `<clipPath>` contents. In the test art the exporter had duplicated the cloak into the clip path, so that duplicate baked as a solid black layer on top of everything, the "hard black cape" that looked like a stroke bug. The baker skips it.

A clipped shape is built on its own, intersected, and only then added to its colour's layer. Intersecting the layer would clip everything already painted into it.

## Rendering an SVG for comparison

Reasoning about a bake from layer numbers does not converge; render it. Edge renders SVG properly and is on every Windows box:

```
msedge.exe --headless=new --disable-gpu --screenshot=out.png --window-size=760,800 file:///page.html
```

with a one-line HTML wrapper holding an `<img src="file:///drawing.svg">`, because the headless screenshot of an SVG loaded directly comes out empty.

To find which layer is at fault, render each one alone: a document with one `<img>` per `-ink` class, tinted, laid out in a grid. That showed the clip working (the clipped layers were correctly confined) while the fills were not.

## Three keyboard schemes at once, without breaking typing

The phase gate asks for a menu navigable by arrows, WASD and IJKL simultaneously. W is also a letter, and a text field has to keep receiving it.

The order settles it. `OnKeyDown` asks for the *literal* key first, `ToRmlKey`, which knows arrows, Enter, Escape, Tab and the editing keys, and only if that comes back unknown asks `ToNavigationKey`, which knows the gamepad and the letter schemes. That second question is skipped while an `input` or `textarea` has focus.

So the two maps are separate on purpose. Folding the letters into `ToRmlKey` would make them directions everywhere, including inside a text field.

## A stick is a position, not an event

Slate reports an analog axis every frame it is off centre, so forwarding it directly either does nothing or navigates every frame. `FAnalogNavigator` records the axes in the input handler and turns them into presses in `Tick`, where there is a clock: past a 0.5 deadzone it emits once, waits 0.4 s, then repeats every 0.12 s, a keyboard's auto-repeat shape.

The larger axis wins, so a diagonal push means one direction rather than two, independent of frame timing. Changing direction without returning to centre counts as a new press, because that is what it feels like to the player.

The navigator is reset on focus loss: a stick held while focus moves elsewhere would otherwise keep repeating into a document that is no longer in front.

Navigation keys are sent down **and up** together. RmlUi navigates on the press, but an element left believing a key is still held misbehaves the next time that key arrives.

## Dev tooling is compiled out of Shipping, except one UCLASS

`FolioConsoleCommands.cpp` and `FolioDebugger.cpp` are wrapped in `#if !UE_BUILD_SHIPPING`, so `folio.Show`, `folio.ShowWindow`, `folio.Gate`, `folio.NavTest`, `folio.Debugger`, `folio.MouseMode`, `folio.ReloadTextures` and `folio.Nav.Bind*` do not exist in a retail build. The `folio.Nav.*` console *variables* are unaffected: they are runtime settings and live in `FolioNavigation.cpp`.

`UFolioBindTest` stays compiled in Shipping deliberately. UnrealHeaderTool does not handle a `UCLASS` inside a preprocessor block: it generates the reflection code regardless and the link fails. An unreferenced class registration costs nothing. To remove it, the supported route is a separate module with `"TargetConfigurationDenyList": ["Shipping"]` in `Folio.uplugin`, which also means fixing the public headers that include private ones (`SFolioCanvas.h` includes `FolioNavigation.h`); no external module can compile against them today.

Verify with a string search, and remember `TEXT()` is UTF-16: searching a binary for ASCII `folio.NavTest` finds nothing either way, which reads as a pass when it is not a test at all.

## The RmlUi debugger lives inside the context it inspects

`folio.Debugger` attaches `Rml::Debugger` to a canvas. What is not obvious:

**One debugger per process, hosted by a context.** `Debugger::Initialise(ctx)` puts its own documents into `ctx`, so it draws through Folio's render interface and receives mouse through the canvas's normal forwarding. Inspecting a second document means `Shutdown()` then `Initialise()` on the other context, never a second `Initialise`.

**RmlUi releases the debugger when its host context is destroyed**, without notice. So the stored host pointer can outlive the debugger, and every entry point in `FolioDebugger.cpp` re-checks the canvas against `SFolioCanvas::GetLiveCanvases()` before touching the API. Context lifetime equals canvas lifetime, which makes that check exact.

**Its font comes through the *memory* `LoadFontFace` overload** as family `rmlui-debugger-font`, not through a file. A custom font engine that only implements the path overload compiles, runs, and renders the debugger's chrome with no text, a symptom that points nowhere near the cause. `FFolioFontEngine` implements both; verified by screenshotting `uipreview --debug`.

**Its documents live in the host canvas's context, so the canvas has to take the mouse.** A HUD sets `bInteractive = false`, which makes the widget hit-test invisible so the game behind it can be played through, and that drops every click meant for the debugger too. The debugger takes the host interactive while it is *visible* and gives it back when it hides: a visible debugger you cannot click is useless, and holding interactivity past that would leave firing dead in a game whose HUD somebody once inspected.

It also needs a cursor, which a game that has captured the mouse will not have. `folio.MouseMode on` sets `FInputModeGameAndUI` with the cursor shown and no viewport lock.

**Slate's Widget Reflector is not an alternative.** A document is one `SFolioCanvas`, so the reflector shows one widget and stops, which is why the accessibility work built proxy widgets. Those proxies appear under the canvas once the tree is built (`folio.Accessibility.Force 1`, then take the snapshot) and carry `FReflectionMetaData` naming them by what they say, so the structure is legible there. It is still worse than the debugger except for confirming the accessibility tree: the proxies are hit-test invisible so the picker cannot select them, they only exist while something is listening, and they carry a node's meaning rather than its styling.

## Hot reload: what decides the granularity, and why the code is duplicated

`folio.HotReload` (default on, editor only) watches `<project>/UI` and `<plugin>/examples` and `<plugin>/rcss` with `IDirectoryWatcher` and reloads open documents. Three things are load-bearing.

**The dependency list is what RmlUi opened, not a parse of `@import`.** `FFolioFileInterface` records every path it successfully opened, so the list covers the document, every imported and linked stylesheet, every font and every texture, and cannot drift from RmlUi's own resolution rules. It only grows, so a file no longer referenced costs a wasted reload rather than a missed one.

**Granularity is not an optimisation.** Rebuilding a document loses everything the DOM held, so an `.rcss` change reloads stylesheets only and leaves focus, scroll and the data model alone. `.rml` rebuilds and puts focus and scroll back via `FolioReload.cpp`. Anything else opened is art: `ReloadChangedTextures` by name plus `ReleaseCompiledGeometry`, deliberately not `Rml::ReleaseTextures`, so the distance field pages are not dropped with it.

**`FolioReload.cpp` duplicates the preview tool's `Reload.cpp`/`ElementPath.cpp` on purpose.** RmlUi is a static library linked into both the Folio and FolioCore modules, so each has its own copy of RmlUi's globals. `Rml::Factory::ClearStyleSheetCache` and `Rml::ReleaseTextures` called from a FolioCore translation unit clear FolioCore's caches, which are empty, and the reload silently does nothing. Anything touching RmlUi's globals has to be compiled into the module that owns the context. Do not "de-duplicate" this into FolioCore.

Two more traps:

- **Editors save by writing a temporary file and renaming over the target**, so for a few milliseconds after a notification the file is locked or zero length, and every other save looks like a parse error. `FFolioFileInterface::Open` retries for 250 ms under `WITH_EDITOR`, but only when the file already exists, so a genuinely missing one still fails immediately.
- **A single save arrives as several notifications** and an atlas re-bake touches many files, so changes accumulate and apply once on the next tick rather than per notification.

**Mock data does not hot reload.** `UI/mock/<model>.json` is read with `FFileHelper`, not through the file interface, so it is not in the dependency list and a change is ignored. Fixing it needs a change to `FFolioDataModel`: it binds the address of the parsed `FJsonValue` node, so a re-parse would leave RmlUi holding a freed pointer. The fix is to bind a stable slot that the definitions dereference, as the preview tool does. Reopen the document for now.

## M_FolioMSDF is not cooked unless the project asks for it

`FFolioRenderInterface` loads the distance field material with `LoadObject<UMaterialInterface>(nullptr, TEXT("/Folio/M_FolioMSDF.M_FolioMSDF"))`. Nothing *references* the asset, so the cooker never sees it, it is absent from every package, and all text renders with no shader. The symptom is one log line, `could not load /Folio/M_FolioMSDF; text has no shader to resolve its fields`, and otherwise looks like a text rendering bug.

Every project using Folio needs this in `DefaultGame.ini`:

```ini
+DirectoriesToAlwaysCook=(Path="/Folio")
```

The plugin cannot declare it: `"CanContainContent": true` makes the content *mount*, not cook. A `UDeveloperSettings` holding a `TSoftObjectPtr` would not help either, since a config soft reference is not a cook reference without asset manager rules. Confirmed: the material was missing from the package until the entry was added, and present afterwards.

## Staging: DirectoriesToAlwaysStageAsUFS is relative to Content, not the project

Folio's files are not assets, so they reach a package only by being staged, and a wrong path base is silent: the entry is accepted, UAT logs `Unable to find directory ... for staging` as a warning, and every screen is empty in a build that worked in the editor.

`StageAdditionalDirectoriesFromConfig` in `CopyBuildToStagingDirectory.Automation.cs` combines each `(Path=...)` against **`ProjectContentRoot`** and stages into `StageContentRoot` (`<RelativeProjectRootForStage>/Content`). Folio resolves against `FPaths::ProjectDir()`, one level up. So every entry starts with `../`:

```ini
+DirectoriesToAlwaysStageAsUFS=(Path="../UI")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/rcss")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/fonts")
```

`..` is safe: `StagedFileSystemReference`'s constructor **collapses** parent references rather than rejecting them, and only throws if the result escapes the staging root. `Kaboom/Content/../UI` becomes `Kaboom/UI`, which is what `FPaths::ProjectDir()` resolves to at runtime.

**A subdirectory of a staged directory cannot be excluded.** `FindFilesToStage` recurses everything except `RestrictedFolderNames` (NoRedist and friends) and drops only `.uasset`, `.umap`, `.po` and `.archive` by extension. That is why Folio's dev documents live in `dev/` rather than `rcss/dev/`, with `/folio/dev/` mounted onto it in `FFolioFileInterface::Resolve`.

Verify from the manifest rather than by launching: `Engine/Programs/AutomationTool/Saved/Logs/FinalCopyWin64_UFSFiles.txt`, or `UnrealPak.exe <pak> -List`. Restaging alone is fast (`BuildCookRun -skipbuild -skipcook -stage -pak` took 3 seconds against an existing cook), so there is no need to package fully to check this.

**Generated font fields are not staged.** `FontCacheDir` is `<Project>/Saved/Folio/Fields`, and Saved is not part of a package, so a packaged build regenerates fields on first use. That is the Latin-1 prewarm cost per face at startup, not per glyph; a prebaked read-only cache staged alongside the faces is the obvious improvement.

## -uidev: loose files ahead of the pak

`-uidev` (or `-uidev=<path>`) makes `FFolioFileInterface` check an override directory before the package, so UI can be changed on a device (a console, a Steam Deck) without cooking again. The directory mirrors the project, which is also the staged layout, so `uidev/UI/screens/hud.rml` overrides `UI/screens/hud.rml`. A file that is not there falls through, so a directory holding one document works. Compiled out of Shipping.

Bare `-uidev` resolves to `FPaths::ProjectDir()/uidev`, which in a staged build is `<Staged>/<Project>/uidev`, beside the `UI` tree it shadows. Explicitly **not** `FPlatformProcess::BaseDir()`: once staged that is `<Project>/Binaries/<Platform>`, where nobody would think to drop files.

The override is logged as a **warning** at startup on purpose. A stale file there presents as the game ignoring an edit, and nothing in that symptom points at an override directory.

## Commands out of a document: BindEventCallback over reflection

`data-event-click="Confirm"` reaches game code because `FFolioObjectModel` binds every `UFUNCTION(BlueprintCallable)` on the bound source as an RmlUi data event callback, mirroring how it binds properties as values. `data-event-click="Damage(0.25, 'a button')"` works too: RmlUi hands the callback a `VariantList` and the arguments are written into the call frame in declaration order.

A function is reachable only if, checked at mount rather than at click:

- It is `BlueprintCallable`, the same "the author marked this as a surface" test the properties use with `CPF_BlueprintVisible | CPF_Edit`.
- It has **no return value.** A data expression discards it, so a function with a result is almost always a query meant to be bound as a value.
- Its parameters are `bool`, a number, `FString` or `FName`, **by value**. An out or reference parameter has nowhere to go: RmlUi passes variants in and reads nothing back.

Anything rejected is logged by name, but **only if the function is declared on the source's own class**. Every base class contributes callable functions nobody meant as UI commands, and naming those would bury the one that matters.

`ProcessEvent` needs one contiguous frame holding every parameter at its reflected offset, and the frame must be constructed and destroyed by the properties themselves rather than memset. An `FString` parameter left as zeroed memory is a string that frees a pointer it never owned. Arguments the expression did not supply keep whatever the property initialised to, so a command with an optional argument still works from `data-event-click="Name"`.

## A data model must exist before the document that reads it loads

RmlUi resolves a document's bindings as it loads and gives no way to attach a model afterwards. It also refuses a second model under a name it already has. Together those constrain the API.

**`UFolioWidget` builds its canvas with no document.** `RebuildWidget` creates the canvas, applies every recorded binding, and *then* calls `LoadDocument`. Passing `DocumentPath` to the canvas's `Construct` loads it immediately, before any `BindViewModel` call can have run, so every binding renders as literal braces with the cause nowhere near the symptom.

`BindViewModel` is therefore callable at any time. It records the binding (so a rebuild, whether designer preview, re-add to the viewport or hot reload, puts the same models back), applies it if the canvas exists, and rebuilds the document if one was already up. That last case logs, because it is not free.

**Mock data is compiled out of Shipping and yields to game code.** Mounting `UI/mock/<model>.json` in every configuration has two consequences: a retail build renders invented values for any screen whose model the game forgot to bind, instead of failing visibly; and the mock occupies the name, so `BindObjectModel` on it fails. So in `SFolioCanvas::MountDataModel` the whole path is `#if !UE_BUILD_SHIPPING`, gated on `folio.MockData`, and skipped when `HasObjectModel` says game code got there first.

`folio.MockData 0` shows a screen as a player would see it if the binding were missing, which is otherwise only discoverable by packaging.

## Localisation: the gather cannot see markup, so a script makes it visible

`GatherTextFromSource` scans C++ for `NSLOCTEXT`. It knows nothing about `.rml`, so a manifest gathered without help silently lacks every string in a Folio document.

`Tools/locgather/gather_rml.py` walks the documents, reads every `data-loc` key with the text beside it, and writes **one generated header that nothing includes**: it exists to be read by the gatherer, not compiled. `scripts/loc.sh` runs it and then `GatherText`, in that order, because running the second without the first is the failure this arrangement prevents. Bash rather than PowerShell so a Linux developer can use it: engine discovery reads the registry on Windows and `~/.config/Epic/UnrealEngine/Install.ini` on Linux, with `UE_ROOT` overriding both. `reg query` emits CRLF and an engine path routinely contains spaces, so parsing its output by last field silently truncates it.

**The UI is its own localization target.** `Config/Localization/Folio.ini` gathers only `Source/Generated/`, producing `Folio.manifest`, `Folio.archive`, `Folio.po` and `Folio.locres`, so the translator's file holds the screens and nothing else. Two consequences that are easy to get wrong:

- **The target has to be registered for the runtime to load it**, with `+LocalizationPaths=%GAMEDIR%Content/Localization/Folio` under `[Internationalization]` in `DefaultGame.ini`. Without it the manifest is correct, the archive has translations, and every string still renders in English with nothing logged. Check this setup mistake first.
- **The game's own target must exclude `Source/Generated/*`**, or the same strings land in two manifests and appear twice in a word count.

**No `FileNameFilters=*.rml` gather step.** Emitting a header means there is no custom filter for the Localization Dashboard to drop. `Config/Localization/Folio.ini` still says never to use the Dashboard for gather settings, because it regenerates that file from its own store.

**The namespace rule is duplicated across two languages and both must agree.** `Folio::Loc::NamespaceForDocument` and `namespace_for` in the script both derive `Folio.MainMenu` from `UI/screens/main_menu.rml`, and `Folio.Common` from anything under `UI/lib/`. If they disagree, every lookup misses and every string falls back to English with nothing logged. The script fails on two documents deriving the same namespace, which stops the file-stem rule silently merging `UI/hud/player.rml` and `UI/screens/player.rml`.

**Runtime resolution is a document pass, not `TranslateString`.** That hook receives a string with no element context, so it cannot tell whether the owning element carries a key: it can neither look one up nor notice text that has none, and wiring both would translate twice. The pass runs in `LoadDocument` before the first update, so translated text is laid out rather than reflowed a frame later.

Three kinds of element are skipped, for different reasons:

- **Containing child elements.** Translation replaces the element's entire contents, so a child would be swallowed into the string and come back as escaped, visible markup. The key belongs on an inner text-only element.
- **Driven by a data binding.** Its value is game state, and rewriting the RML destroys the view RmlUi built at load, leaving the binding permanently dead.
- **Empty.** Nothing to gather, so nothing to resolve.

`FText::AsLocalizable_Advanced` rather than `FindTextInLiveTable_Advanced`: the former returns the source string for a key that is gathered but untranslated, which is every string in the culture it was authored in. The latter is used only by `folio.Loc.Report`, where the question is whether a translation exists at all.

**Verification.** `folio.Loc.Report [document]` lists every key with `[loc]` or `[source]`. Run it in a `-game` session rather than PIE:

```bash
UnrealEditor.exe <project>.uproject -game -windowed -resx=640 -resy=360 `
  -ExecCmds="folio.Loc.Report UI/screens/settings.rml" -log=Loc.log
```

To prove text swaps, patch a translation into `Content/Localization/Folio/en/Folio.archive` and regenerate only the locres. **`-DisableSteps` does not work**: GatherText ignores it and executes every step, and step 3 rewrites the patched translation back to the source text. Use a throwaway config holding `[CommonSettings]` plus a single `[GatherTextStep0]` of `GenerateTextLocalizationResource`. Invoking that commandlet directly with `-run=GenerateTextLocalizationResource` crashes: it has to go through `-run=GatherText`.

The manifest and archives are **UTF-16**, so a UTF-8 decoder fails on the first byte.

## The scripts are bash, including the Windows-only ones

Every script under `scripts/` is `.sh`, including `build.sh` and `build_ue_libs.sh`, which are Windows-only today because they build MSVC static libraries. A `.sh` can grow a Linux branch later and a `.ps1` never can, so when Folio builds on Linux `folio_import_msvc` gains a sibling in `vsenv.sh` rather than a second script elsewhere.

The logic-heavy ones are python behind a thin `.sh` wrapper, as `loc.sh` is: `Tools/shaderparity/check_parity.py`, `Tools/verify/verify.py`, `Tools/verify/visual_tests.py`. They read and compare JSON, which a shell does badly, and python is already a dependency.

**Importing the MSVC environment from bash needs a generated batch file.** The environment lives in a cmd process, so it has to be dumped with `set` and read back. `cmd //c "\"$vsdevcmd\" ... && set"` does not work: bash's escaping and MSYS's path mangling fight over the quoting and cmd sees a literal `\"` as the command name. Writing a one-line `.bat` that does its own quoting on the cmd side, then running it, is reliable. Further traps in the same place:

- **`MSYS_NO_PATHCONV=1` suppresses the `//c` to `/c` rewrite**, so cmd receives `//c`, ignores it, and opens an interactive shell that returns its banner and hangs. Use one or the other, never both.
- **Only `PATH` may be converted to POSIX form.** `cygpath -up` it so bash can resolve `cl.exe`, and leave `INCLUDE`, `LIB` and `LIBPATH` as Windows paths, because the compiler reads them natively.
- Names cmd exports that are not legal shell identifiers have to be skipped, or `export` is a syntax error rather than a warning.

**The ports were checked against the PowerShell originals.** `check_parity.py` reports the same 779 tokens, all 40 metrics reports from `verify.sh` are byte-identical to the PowerShell run's, and the visual suite renders 109 of 109. PowerShell stringified a dp ratio of `1.0` as `1`, so report and screenshot names stay `name@1` rather than `name@1.0`, and a baseline directory recorded before the port still compares.

## Two-way binding has to write through the property, not into the mirror

`data-value` on an input writes back through the data model. Binding with `Constructor.Bind(name, &Field->FloatValue)` hands RmlUi the address of the mirrored copy, so a drag sets the mirror, the next `Refresh` overwrites it from the `UPROPERTY`, and the value moves for one frame and springs back. Silent, and worse than refusing.

So the bridge uses a custom `Rml::VariableDefinition` bound by the address of the **field record** rather than of the mirrored value, so `Set` can reach the source object. `FFolioObjectModel::Write` converts by kind: `SetPropertyValue` for a bool, `SetIntPropertyValue`/`SetFloatingPointPropertyValue` for a number, and `ImportText_InContainer` for everything else, the exact inverse of the `ExportTextItem` the read path uses, so an FName, an FText, an enum by name or a struct all round-trip.

Three things that are not obvious:

- **It reads back after writing** rather than trusting the value it wrote. A property may clamp, quantise or round, and the document has to show what the object holds.
- **It broadcasts the field change** when the source notifies. A settings screen showing one value twice (a slider and the number beside it) is the ordinary case, and without the broadcast only the control that was touched moves.
- **`BlueprintReadOnly` and `EditConst` mean refuse, with one warning per field.** A slider bound to a read-only value will not move, and the reason is otherwise invisible. A range control writes once at load, so the warning appears as the document opens rather than when anything is dragged.

**Driving a control from code needs the change event, not only the value.** `DataControllerValue` listens for `EventId::Change` and reads its `value` parameter, so `SetValue` alone updates the control and the model never hears. Code that sets a control has to dispatch `change` itself afterwards, as a widget does when a person moves it. A checkbox's state is the `checked` attribute, so that is what a click toggles.

Verified through that command in a `-game` session: int 50 to 73, float 1.0 to 1.8, bool false to true, string 'Player' to 'Kaede', and a `BlueprintReadOnly` float refused with the warning naming it.

## Idle cost: RmlUi already tells us when it next needs updating

RmlUi 6.2 has `Context::GetNextUpdateDelay()`, public and documented for this, so no patch is needed. Upstream already requests updates for the cases conservative tracking would get wrong:

| Requester | Case |
|---|---|
| `Element.cpp` | a running `animation` or `transition` |
| `WidgetTextInput.cpp` | the blinking caret |
| `WidgetSlider` / `WidgetScroll` | a held arrow's auto-repeat |
| `Context.cpp` | smooth scrolling |

So no re-basing cost across an RmlUi bump, and the animation case is upstream's. `SFolioCanvas::Tick` reads the delay after each update and skips the frame until it elapses; everything on Folio's side of the boundary calls `MarkDirty` with a reason.

**Read the delay after `Update()`, never before.** RmlUi resets it to infinity as `Update` begins and fills it in as elements ask, so before the call it says nothing about the coming frame.

**A captured viewport sends a mouse move every frame**, whether the cursor moved or not. With `MarkDirty` at the top of `OnMouseMove`, a static document reported "never, until input" from RmlUi and still updated 10,489 times in 90 seconds. `OnMouseMove` compares the rounded context-space position and modifiers against the last event and returns early when they match, which took the same document to **2 updates**. It compares in the coordinates RmlUi would receive rather than by `GetCursorDelta`, because a document moving under a stationary cursor is a real move and the delta would miss it.

**`autofocus` is an attribute, not a property.** `ElementDocument::Show` defaults to `FocusFlag::Auto` and scans for the `autofocus` *attribute*; the RCSS `focus` property has nothing to do with it. A caret test that reports "never" may have a field that never had focus, so no caret to blink.

**Both directions are checked, by `scripts/check_invalidation.sh`.** A build where every document idles forever would pass a check that only tested idling, and a frozen document is far worse than a slow one. Measured: static 2 updates in 20 s reporting "never"; animated 1777 updates reporting `0.000s`; a focused caret 19 updates on a 0.698 s timer. It drives a real editor, because what it measures exists only on the Unreal side; uipreview has its own frame loop and would prove nothing.

## Recording a document instead of drawing it

`FFolioRenderInterface::BeginRecording` installs an `FFolioDrawList` where the Slate element list would be, and every draw path falls into it instead. RmlUi does not know the difference: it is the same `Context::Render()` over the same markup.

`FFolioDrawList` and `FFolioDraw` are shared with the layer stack, which already recorded on the game thread and replayed with RHI. So there is **one** representation of a recorded draw, and the two replays differ only in transform and destination: a layer uses an orthographic one into an offscreen target, a world document a world-view-projection one.

**Layer effects are refused while recording, deliberately and loudly.** A layer is an offscreen target composited back into whatever is below it, and a recording has nothing below it. `PushLayer` returns 0 with a one-time warning naming `box-shadow`, `filter`, `backdrop-filter` and `mask-image`; `PopLayer`, `CompositeLayers`, `EnableClipMask` and `RenderToClipMask` are no-ops. The geometry *inside* the effect still reaches the recording, because every draw path falls through to it when no layer is open, so an element with a box-shadow draws without its shadow. `settings.rml` triggers this: it is the common case.

Clip masks are refused for the same reason: a stencil belongs to a layer. Clipping a world-space document is the surface's business, not the document's.

**Gradients and shader decorators needed real support, not a gate.** Both rasterise to a texture and were reachable only through `FSlateResourceHandle`, which a recording cannot use. `GetGradientTexture` and `GetEffectTexture` return the texture record, so the Slate path takes `->Handle` and the recording takes the `UTexture`. The gradient path also scales its UVs by the box size in the recording, because Slate does that through `DrawGeometry`'s UV scale argument and there is no equivalent step here.

**The recording is only rebuilt when the document updated.** `FFolioDocumentHost::Update` returns whether it did, which makes a settled world document cost nothing, the same reason the screen path lets Slate keep its cached draw elements.

Verified with `folio.WorldTest UI/screens/settings.rml` then `folio.WorldTest report`: 48 draws, 1916 triangles at 1000x600, from the project's real settings screen. The report has to wait for the component's first tick, or it reports zero and means nothing.

## RmlUi culls text that falls entirely outside the context

`ElementText::OnRender` (`ElementText.cpp:176-199`) tests each laid-out line against the scissor region and, when nothing has set one, against `Rectanglei::FromSize(render_manager.GetViewport())`, the context's dimensions. A line whose box does not intersect that rectangle is never submitted.

So a world-space document cannot draw text outside its own bounds. Partly outside is fine (corner elements hanging off an edge render in full, because the line intersects). Fully outside draws nothing, with no warning.

This is upstream behaviour, not Folio's recording, and is not configurable without setting a scissor region. If something has to appear clear of the document's background, the background has to be a panel inset inside the document, as `examples/crt_terminal.rml` does for its banner.

Backgrounds and images are not culled this way, only text, which is what makes it confusing: a `div` with a colour appears outside the bounds and the text inside it does not.

## Two colour spaces for a world document, decided by its draw mode

A recorded colour is a byte triple, and what has to happen to it depends on where it lands.

- **After tonemap.** The tonemapper has already applied the output device transform (the LUT in `PostProcessTonemap.usf` ends with `OutDeviceColor`), so scene colour there is gamma encoded. A byte colour goes through as written, `value / 255`, with no decode.
- **In scene.** Scene colour before post-processing is linear HDR with the view's pre-exposure baked in. The byte colour must be decoded from sRGB *and* multiplied by `View.PreExposure`, because the tonemapper divides that back out later.
- **A layer, or a Surface document's target.** An sRGB render target, which encodes on write, so the decode is needed and the pre-exposure is not.

Getting this wrong is quiet: everything renders in the right place at the right size, and the colours read as "a bit dark" rather than as a bug. `FFolioDrawList::SetDecodeSrgb` is the switch, and the layer path and the world path disagree about it on purpose.

## A scene view does not expose the render-resolution view rect

`FSceneView` has `UnscaledViewRect` and `UnconstrainedViewRect`, both at display resolution. The render-resolution rectangle lives on `FViewInfo`, private to the Renderer module, so a scene view extension drawing its own geometry cannot set a viewport matching the view.

Instead, set the viewport to the whole target and correct in the vertex shader, where the view uniform buffer knows: `View.ViewRectMin` and `View.ViewSizeAndInvSize`. Clip space to the view's pixels, then back out to the whole target's clip space. `FolioWorld.usf` does this whenever `FolioSceneTarget.z` is set.

Deriving the rectangle from the display rect and a screen-percentage fraction would be wrong under dynamic resolution, and wrong in split screen where the rect's origin is not zero.

## FPostProcessingInputs is an internal renderer header

`PrePostProcessPass_RenderThread` receives it, and it lives in `Engine/Source/Runtime/Renderer/Internal/PostProcess/PostProcessInputs.h`. UBT does not put a dependency's `Internal` folder on a dependent module's include path, so `Folio.Build.cs` adds it by hand against `EngineDirectory`.

That is an engine-layout assumption. Check it on any engine upgrade: if the path moves, the in-scene draw mode stops compiling, which is at least loud.

## Instanced stereo does not reach the world pass, and `vr.InstancedStereo` lies about itself

World documents work under instanced stereo untouched; do not "fix" it. Both direct routes hook post-processing (`SubscribeToPostProcessingPass(Tonemap)` and `PrePostProcessPass_RenderThread`), and post-processing runs **per view**, after the instanced passes have resolved. An eye index only matters to a pass that draws both eyes in one draw, which neither is. `FolioWorld.usf` therefore needs no `EyeIndex` and reads one set of view uniforms correctly.

Separately: `vr.InstancedStereo` is `ECVF_ReadOnly`, so the console cannot set it, and querying it reports "last set by constructor" even when instanced stereo is on. It is not the authority. `FStereoShaderAspects` reads the ini directly per shader platform through `FShaderPlatformCachedIniValue`, so the value that decides shader compilation comes from `[/Script/Engine.RendererSettings] vr.InstancedStereo`, not the runtime variable. Do not judge it by the console readout.

Emulated stereo (`-emulatestereo` on a standalone launch) exercises the two-view path without a headset, and is how the world pass was verified in both plain and instanced stereo.

## A `transform` reaches the geometry only if the path applies it

`SetTransform` stores the matrix, and `DrawGeometry` composes it for the Slate path. The recording and layer paths (every world-space document, in all three draw modes) took the translation and nothing else, so `transform` in RCSS was dropped silently. It works on screen and does nothing on a surface; the giveaway is an animation where `opacity` moves and `transform` does not, because opacity rides on the vertex colour.

Neither a recording nor a layer replay carries a transform: both draw in document space with a plain offset. So the transform is baked into the vertex positions in `PositionedVertices` before the draw, which keeps the no-transform case free: it returns the compiled vertices untouched.

Anything else added to those paths has the same trap: whatever `DrawGeometry` composes for Slate has to be composed there too, or it silently applies to screen documents only. `RenderShader` (gradients and shader-backed decorators) goes through `PositionedVertices` on both paths for the same reason.

`./scripts/check_world_path.sh` catches this. It asserts the recorded geometry responds to a property rather than comparing images, so it survives a test document being edited and names the property that was dropped. Run it after touching anything in the render interface.

It covers the **layer** path too, by rendering a document into a target through `BeginSurface` and reading the pixels back. Three questions answer most of what can go wrong: did anything draw, is it more than one colour, and is it white. The last is not arbitrary; see below.

## A window's framebuffer is not a render size, and asking for a bigger one fails quietly

`uipreview` renders through a real GLFW window, and a window larger than the display is silently clamped to the monitor's work area, so `--size 3840x2160` on a 1080p display would produce a 1080p-tall image and report success.

Everything RmlUi draws happens at the viewport size in its own layer framebuffers; only the final present in `EndFrame` is bound to the window. `patches/RmlUi/0002-gl3-target-framebuffer.patch` adds `SetTargetFramebuffer` so that one blit can go elsewhere, and the backend creates an offscreen target whenever the window is hidden and its framebuffer does not match what was asked for.

Three things have to move together: the target itself; `Backend::GetFramebufferSize` reporting the *target's* size rather than the window's, or the capture reads the requested width and the window's height; and `CaptureBackBuffer` binding the target and reading `GL_COLOR_ATTACHMENT0` rather than `GL_BACK`, since an offscreen target has no back buffer and `glReadPixels` fails with `GL_INVALID_OPERATION`.

Verified at 1280x720, 2560x1440, 5120x2880 and 7680x4320 on a 1440p display: all render at exactly the size asked for, and the 4x render downsampled matches the 1x render to a mean of 2.8/255.

## Comparing the two renderers: match dp, and compare coverage rather than colour

`folio.Surface.Shot <doc> <out.png> [w] [h]` is the engine's answer to `uipreview --screenshot`. It renders through the surface path rather than screenshotting the viewport, so the image is the document and nothing else, on a transparent ground.

Two things have to be right before a comparison means anything:

- **Pin the dp ratio on both.** The preview's ratio follows its viewport by default (`--dp-reference`, 1080), and the engine host is told one directly. Unmatched, every length differs and the images look like a layout bug. `uipreview --dp-ratio 1` against a host updated with a ratio of 1 lines the two up exactly.
- **Compare coverage, not RGB.** The engine writes premultiplied colour on a transparent ground; the preview composites onto an opaque one, and does no sRGB conversion where Slate linearises vertex colour and applies display gamma. The same document is a different colour in each and the same shape in both.

`./scripts/compare_renderers.sh` does the comparison without diffing images. Both sides emit the same metrics schema (`uipreview --metrics-report` and `folio.Metrics.Report`), and it compares boxes, font sizes and where every text line landed. Exact, no tolerance to tune, and a failure names the element rather than a pixel count. `Folio::ComputeElementPath` is shared between the two so they cannot disagree about what names an element, which would report a naming difference as a divergence.

First run: all five documents agree exactly, 109 text elements between them.

## A generated image has no UTexture, and the non-Slate paths bind white without one

`RegisterPixels` builds an `FSlateDynamicImageBrush` and a Slate resource handle, avoiding the UTexture path. That is fine for Slate and wrong for everything else: the recording and layer replays bind an `FRHITexture` straight off a `UTexture`, so without one a generated image resolves to nothing and the shader samples `GWhiteTexture`.

Symptom: **every gradient decorator is a white rectangle on every world-space document, in all three draw modes**. It reads as a rendering bug rather than a missing resource, because a draw did happen; white is what sampling nothing looks like.

So `RegisterPixels` also creates a transient `UTexture2D`. Ordered rather than flushed: creating it enqueues the resource init and whatever draws with it enqueues afterwards, so it is ready by the time the pass runs. Anything else that generates pixels for a non-Slate path needs the same.

## Anything touching an RmlUi global must be compiled into the module that called Rml::Initialise

RmlUi is a static library linked into **both** `Folio` and `FolioCore`, so each DLL carries its own copy of every RmlUi global. Only the Folio module calls `Rml::Initialise`, so only its copy of `StyleSheetSpecification` is populated. Code compiled into FolioCore that reaches a global reads one that was never built.

This is why `FolioFontEffects.h`, `FolioFilters.h` and `FolioBehaviours.h` are header-only, with comments saying to instantiate them from the module that initialised RmlUi. Compiled into FolioCore, `Element::SetProperty` resolves the property through `StyleSheetSpecification::GetProperty`, which reads `properties.size()` on an empty vector: every drag crashed on the first mouse move with a stack pointing at `std::vector::size` that looked like a lifetime bug.

Element-local calls (`SetClass`, `GetAttribute`, `AddEventListener`) have no globals behind them and work from either module, which is what makes this misleading: most of a file works, and one call in it does not.

The rule: a `.cpp` under `Source/FolioCore/Private/` may not call `Rml::Factory::*`, `Rml::StyleSheetSpecification::*`, `Element::SetProperty`, or anything else that resolves through RmlUi's registries. Put it in a header and let the consuming module instantiate it.

## Removing an RmlUi element inside its own event handler frees it under the dispatcher

`Element::RemoveChild` returns the only owning `ElementPtr`. Dropping that return value destroys the element there and then, and the handler that decided to remove it is running inside that element's own event dispatch, so RmlUi carries on walking a freed object. It segfaults on the next listener.

This hit `folio-drop="remove"`, where the removal waits for the `animationend` that RmlUi delivers to the element being removed. Keep the returned `ElementPtr` in a member and release it on the next event, once the triggering dispatch has finished.

The same applies to anything that frees an element from a handler: a drop action, a close, a data binding that rebuilds a subtree. Take it out of the tree during the event; free it after.

## Never hold an RmlUi element across frames as a raw pointer

A raw `Rml::Element*` held between `dragstart` and `dragend` (as `FFolioBehaviours` once did for the dragged element) can be freed inside that window, three ways in ordinary use: a document reload frees everything in it, `folio-drop="remove"` frees one outright, and a drag interrupted by either never delivers the `dragend` that would have cleared the pointer. The next mouse move then writes `left` and `top` into freed memory, which surfaces as a crash inside `std::vector` under `Element::SetProperty` and reads as an RmlUi bug rather than a lifetime one.

Use `Rml::ObserverPtr<T>`: `Element` derives from `EnableObserverPtr<Element>`, so `element->GetObserverPtr()` gives a pointer that goes null when the element is destroyed. Anything held across frames, or across an event dispatch that might reload the document, has to be one.

`GetObserverPtr()` returns `ObserverPtr<Element>` even on an `ElementDocument`, because the observable base is `Element`. No covariant version exists: store `ObserverPtr<Element>` and use `GetOwnerDocument()` when a document-only method is needed.

The same rule covers listeners in the other direction. RmlUi calls `OnDetach` on every listener an element still holds when it is destroyed, so a listener freed while still registered crashes inside the document's destructor. `FFolioBehaviours` detaches from its own destructor for that reason.


## Accessibility tree and Studio blur parity

The accessibility walk rebuilds the whole tree for a changed document rather than patching it. RmlUi has no change notification covering everything a document can do to itself, and a tree silently one edit behind is worse than no tree, so do not replace the walk with incremental updates.

`scripts/studio.sh parity` reports a known difference for a blur that samples past the viewport edge, such as a full-screen inset `box-shadow`. Desktop OpenGL reads nothing past the edge, while WebGL cannot clamp a texture to a transparent border and reads the edge pixel: up to 14 levels out of 255 on a few edge pixels. Do not chase it as a renderer bug.


## Supersampling: the target scales, the document does not

**The sample count is a property of the target.** The document lays out, shapes, breaks lines, bakes its glyph atlas and hit-tests in screen pixels at every sample count; the layers it draws into are allocated denser, and the replay's transform stretches one space onto the other. Multiplying the dp ratio by the sample count looks equivalent and is not: the font atlas bakes at the bigger size (four times the area, every glyph resampled on the way to the screen), line breaking and rounding happen in a different space so a line can break differently, and hit testing, scroll offsets and reported sizes all move into that space. `folio.Screens` should print the same numbers whatever the sample count; if it does not, the document is being scaled somewhere.

**A blur radius is the one thing that follows the target.** It is expressed in document pixels and applied to target pixels. Scale the sigma **and** the kernel's step: a kernel capped at a tap count reaches half as far when the sigma doubles, and a wide shadow then ends in a visible edge rather than fading out.

**Text is held out of the target.** A glyph run drawn into the surface is recorded rather than rasterised, and drawn to the screen after the composite at one document pixel to one screen pixel: the same geometry, atlas and shader constants an unsupersampled document uses, identical to the pixel. A run goes back through the target when it is inside a layer (filter, shadow, mask), under a clip mask (a stencil the surface owns, with no screen equivalent), or under something painted later. Testing each draw against the waiting runs:

- test against what a layer **painted**, not the region it was allocated. A layer is sized to the current clip, which for an unclipped element is the whole screen, so allocated-region tests put every run back the first time anything casts a shadow;
- widen that by the blur, the one thing that puts colour outside the geometry that made it;
- split a covered run **per glyph**. A run is a whole line, so a card overlapping the last word otherwise costs the sentence;
- put a run back where it arrived, with the clip it arrived under: by the time something is found covering it, the clip in force belongs to the coverer.

A document ending with a full-screen sheen, scrim or vignette keeps all its text in the target. That is the mechanism working.

**Resolve in a pass, never through the sampler.** Drawing the big target with a quad and letting bilinear filtering shrink it averages the block only when the quad's texture coordinates land exactly on texel corners: a whole-pixel rectangle, exactly the sample count, at an integer position. Anywhere else (a fractional window size, a rounded context size, a half-pixel offset) each output pixel reads one texel of an unfiltered raster, which is nearest-neighbour decimation and harder edges than no supersampling, uniformly across the screen. `ResolveSupersample` point-samples the NxN block behind each destination pixel and divides, and the result is composited with a quad snapped to whole pixels so the last step is a copy. Hard columns on a shallow diagonal: none 72, 2x through the sampler 62, 2x resolved 26, 4x resolved 21.

**A background's antialiasing skirt is one screen pixel wide at every sample count.** The geometry is then identical however densely it is rasterised, so samples add to the skirt instead of replacing it, and supersampling can never come out harder than not supersampling. It also means a sample count change needs no regeneration: backgrounds are cached, and regenerating them is a restyle, which replays entry animations and makes a settings toggle look like a screen reload.

**A depth-stencil per layer only when a clip mask needs it.** Check the recorded draws and skip it otherwise, or a document with no masks pays a full-size depth-stencil per layer per frame.

**Scope inside the render-thread lambdas.** A layer is replayed, blurred, masked and resolved on the render thread, and a scope on the game-thread function that queued it ends at the enqueue, so milliseconds report as microseconds. Every render-thread lambda carries its own scope and cycle counter, and the graph builder a GPU stat scope. Measured at 4x on one screen: about 1 ms of GPU for the whole UI against a 20 ms scene, with the layer pool holding 316 MB at a 1915x1270 context.

**An unchanged frame reuses the target.** The layout reports whether it updated, and the held-back runs are kept from the last render rather than dropped, so a still screen composites the existing target and draws only its text.

# Notes

Gotchas found while building this. Read before debugging anything surprising.

## RmlUi version and documentation

**Pinned to RmlUi 6.2** (released 2026-01-11), the latest stable release.

**The docs site lies about what is in the release.** <https://mikke89.github.io/RmlUiDoc/> is
built from RmlUi's `master` and shows no version banner, so features merged after 6.2 are
documented as if they exist. Check `changelog.md` in the pinned source tree, not the website.

Known cases:

- **CSS custom properties and `var()` do not exist in 6.2.** Merged to master 2026-06-07
  (PR #947) with follow-up bugs still open (#965 transitions with `var()`, #966 `var()` with
  dp-ratio changes). `ui/tokens.rcss` therefore expresses design tokens as **concrete classes**,
  not variables. Revisit at RmlUi 7.0.
- **`:disabled` is not an RCSS pseudo-class.** Use a `.is-disabled` class. `:focus`,
  `:focus-visible`, `:checked`, `:hover`, `:active`, `:nth-child()`, `:not()` and `:scope` are
  all supported.
- **`border-radius` does not support percentages or elliptical (two-axis) radii.**
- **`Rml::ReleaseMemoryPools()` does not exist** in 6.2. Not available for the reload path.

## The fork named in the original brief is dead

`git@github.com:viciious/RmlUi.git` was last pushed **August 2019**. It predates RmlUi 3.0, so
it has no flexbox, no media queries, no data bindings, and the entire pre-6.0 immediate-mode
render interface. We build against upstream `mikke89/RmlUi`.

## Backend choice: GLFW + OpenGL 3.3

`RmlUi_Renderer_GL3` is the only shipped renderer implementing the full 6.x feature set:
render layers, `CompileFilter`, `CompileShader`, clip masks, `SaveLayerAsTexture`. GL2 and the
SDL_Renderer backend silently drop box-shadow, filters and gradients, which the component
library depends on. GLFW additionally gives a hidden-window mode for headless screenshots.

**The backend is forked** into `Tools/uipreview/src/backend/` rather than used as shipped,
because the upstream one constructs its `RenderInterface_GL3` in a file-static struct with no
seam to substitute a subclass, creates its window without letting us set `GLFW_VISIBLE`, and
does end-frame and buffer-swap atomically (screenshot capture has to happen between the two).
The fork records its upstream commit in a header comment and stays minimally diverged so
diffs remain readable across an RmlUi bump.

## Which advanced rendering features Folio implements

**Target: all of them. Implemented so far: geometry, textures, scissor, transforms, clip
masks and the gradient shaders.** Layers and filters are not, and until they are, anything
that reads a layer back does not work. Status table at the end of this section.

**All of them, eventually.** The Unreal render interface implements the complete RmlUi 6.x optional
feature set, not a subset. Decided deliberately: a feature that is absent does not fail
loudly, it parses and draws nothing, so a missing one is discovered months later as "why is
this panel flat" rather than as an error. Retrofitting one also means revisiting every
document authored against its absence.

| Render interface feature | What stops working without it |
|---|---|
| `EnableClipMask` / `RenderToClipMask` | `border-radius` and `overflow: hidden` on anything transformed. Clips to the wrong rectangle |
| Render layers (`PushLayer` / `PopLayer` / `CompositeLayers`) | `opacity` on a subtree, `filter`, `backdrop-filter`, `mask-image` |
| `SaveLayerAsTexture` | `backdrop-filter`, and any effect reading what is already drawn |
| `CompileFilter` / `RenderFilter` | `filter` and `backdrop-filter` entirely: blur, drop-shadow, brightness, contrast, grayscale, invert, opacity, saturate, sepia, hue-rotate |
| `CompileShader` / `RenderShader` | Every gradient decorator, and any custom decorator |
| `SetTransform` | `transform`, so every animation that is not opacity or colour |

`box-shadow` needs no interface feature of its own but is drawn through the layer and clip
mask paths, so it comes with them.

### Current status

| Feature | State |
|---|---|
| Geometry, textures, scissor, `transform` | Done, through `MakeCustomVerts` |
| Gradients (every `*-gradient` decorator) | Done, by baking the gradient into a texture rather than compiling a shader. The decorator's texture coordinates are pixel offsets inside the element box, which is the same space the shader parameters use, so the gradient can be evaluated on the CPU once and sampled |
| Clip masks | Approximated. Slate clips with a quad rather than a stencil, so a mask becomes the bounding box of the geometry that set it: exact for rectangles, square-cornered for a rounded one, and `SetInverse` cannot be expressed at all |
| Render layers | Implemented as render targets, drawn into with `FCanvas` on the game thread. Pooled by size |
| `filter: blur` | Implemented as a separable gaussian global shader over the layer target |
| Other filters, `backdrop-filter`, `mask-image` | Accepted and ignored. The layer machinery is there; only the shaders are missing |
| `box-shadow` | **Composites, but the colour is wrong.** Still banned in the conventions |

### box-shadow, and why the layer path draws with RHI rather than FCanvas

It works. Getting there turned on one thing: **an outer box-shadow must not paint inside the
element it belongs to**, and RmlUi says so with `ClipMaskOperation::SetInverse` - "everywhere
except this shape". A clipping rectangle cannot express a complement, and `FCanvas` exposes no
stencil state, so the layer path draws with RHI: recorded on the game thread as RmlUi issues
the calls, replayed on the render thread in one pass per layer. Recording is what lets the
stencil live for a whole layer, which a per-draw transient graph cannot do.

The symptom before that was a panel measuring `(20,20,26)` where `#1f1f28` was expected, and
the arithmetic named the cause exactly: `31 x (1 - 89/255) = 20.2`, i.e. the surface darkened
by precisely the shadow's own alpha, because the shadow was painting over its own interior.

Three colour fixes were made before that and none of them moved the number - gamma, a
`CompositeLayers` that popped and re-pushed (which clears the layer it is about to composite),
and straight-alpha blending against premultiplied data. All three were real bugs. None was
*the* bug. **A fix that changes nothing is evidence, and after the first one the right move
was to measure rather than guess again.** Reading the layer's pixels back took one build and
settled it.

Gamma still had to be right once the draw path was ours: the target is sRGB and the shader's
output is encoded on write, so vertex colours are decoded going in. `pow(2.2)` is not close
enough - it lands `#1f1f28` on `(22,22,34)` because the real sRGB curve has a linear toe and
UI colours are dark, which is exactly where the two disagree. `FLinearColor::FromSRGBColor`
gives `(28,28,38)`. The remaining 3/255 is the 8-bit round trip through the target.

**The custom shader decorator is in scope for Phase 3, not later.** Additive and screen
blending are not reachable from RCSS and do not require forking RmlUi: a decorator compiles a
named shader through `CompileShader(name, dictionary)`, and `RenderShader()` draws it with
whatever blend state we choose. `Rml::DecoratorShader` is the reference implementation. This
is the hook that makes HUD work possible at all - hit flashes, damage vignettes, charge and
cooldown sweeps, scanlines, dissolves, holographic panels, anything additive. Adding it after
the component library exists means reworking every screen that assumed it was unavailable.

## Rendering

- **Colors and textures are premultiplied alpha** in RmlUi 6.x (they were straight alpha in
  5.x). Blend func must be `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`.
- **The 6.x render interface names are `CompileGeometry` / `RenderGeometry` / `ReleaseGeometry`.**
  5.x's `RenderCompiledGeometry` / `ReleaseCompiledGeometry` are gone, and `CompileGeometry` is
  now a required pure virtual rather than an optional optimisation.
- **Compiled geometry vertex and index data stay valid until `ReleaseGeometry`**, so a render
  interface may retain pointers into it instead of copying.
- `SetScissorRegion` uses window coordinates with a **top-left origin** and is unaffected by
  `SetTransform`.

## There is no user-agent stylesheet, and documents shrink to fit

Two layout defaults that make a first document look broken in ways that are nothing to do
with the markup being wrong.

Folio ships `rcss/folio.rcss` covering both, reachable as `/folio/folio.rcss` from any content
root. It counts as engine integration rather than game UI: it establishes what elements mean,
not what they look like.

**Every element defaults to `display: inline`.** RmlUi ships no equivalent of a browser's
user-agent stylesheet, so `div`, `p` and `h1` are inline until something says otherwise, and
a document without a base stylesheet renders as one run of overlapping text.
`Samples/assets/rml.rcss` in the RmlUi tree is sample content, not part of the library. A base
stylesheet establishing block display is mandatory, not a convenience.

**An unstyled scrollbar eats the entire content width.** Scrollbar elements have no default
size either, and `width: auto` on a `scrollbarvertical` resolves to the container's full
content width. So a container styled `overflow: auto` lays out correctly right up until its
content actually overflows, at which point the scrollbar appears, takes all the width, and
every child collapses to its own padding and border. Text inside a child with
`overflow: hidden` disappears entirely. Measured: rows in an 852px list dropped to 32px the
moment a fourth row pushed them past the container height, and back to 842px once
`scrollbarvertical { width: 10dp }` existed. It reads as a flex bug and has nothing to do
with flex - a `display: block` container fails identically.

`folio.rcss` therefore sets scrollbar geometry as well as block display. Geometry only; the
look belongs to the game.

**A document is sized shrink-to-fit, and `display: block` does not change that.** The document
element is laid out against the context as if it were floating, so its width collapses to the
widest line of content. Observed directly: an 800px context rendering a document whose box
measured 681px, exactly the width of the longest text plus padding. Set `width: 100%` (and
`height: 100%` if a full-viewport background is wanted) on the document element explicitly.

## Document lifecycle

- **`ElementDocument::Close()` is deferred.** The document is not destroyed until the next
  `Context::Update()`. Reloading without an `Update()` between close and load leaves the old
  document alive.
- **`ReloadStyleSheet()` preserves the DOM, focus, scroll and data-model state** and clears the
  relevant caches itself. It does **not** reapply inline `style=""` attributes, which is one
  reason inline styles are banned in the conventions.
- **`Element::Focus(bool focus_visible = false)` defaults to false.** Restoring focus after a
  reload without passing the captured value silently drops the focus ring and looks like a
  `:focus-visible` bug.
- **Initial focus is not automatic.** A freshly shown document has no focused element until
  something calls `Focus()`.

## Diagnostics

**Almost every RCSS problem is logged at `LT_WARNING`, not `LT_ERROR`** — unknown properties,
invalid decorator types, bad spritesheet resolution, duplicate `@decorator` names, missing
textures. `LT_ERROR` is essentially just `Log::ParseError`, i.e. RML/XML structural failure.
A validity check that only looks at errors will pass a thoroughly broken stylesheet. That is
what `--strict` exists for.

`SystemInterface::LogMessage` returning `false` **interrupts RmlUi execution**. Only ever do
that for `LT_ASSERT`.

## Unreal builds without RTTI, and RmlUi uses `dynamic_cast`

**Build RmlUi with `RMLUI_CUSTOM_RTTI`, and define it in the consuming module too.**

Unreal compiles with `/GR-`. RmlUi's `rmlui_dynamic_cast` is a plain `dynamic_cast` unless
`RMLUI_CUSTOM_RTTI` is set, in which case it uses RmlUi's own type identifiers instead.
Building the library with `/GR-` and without that option compiles and links cleanly and then
dies at the first cast, which is `Context.cpp:70` inside `Rml::CreateContext` - so the editor
crashes the moment a context is created, with no message and no usable callstack.

`RMLUI_CUSTOM_RTTI` is a **PUBLIC** compile definition in RmlUi's CMake, so the header picks a
different `rmlui_dynamic_cast` depending on it. The library and every consumer must agree, or
it is an ODR violation that shows up the same way. It is set in `cmake/ue/CMakeLists.txt` and
again in `RmlUiLibrary.Build.cs`; changing one without the other reintroduces the crash.

## Slate ships its own msdfgen, and it does not collide with ours

Unreal 5.8 has an amalgamated msdfgen at `Engine/Source/ThirdParty/msdfgen/`, compiled straight
into SlateCore for its own SDF text path (`ESlateShader::SdfFont`, `MsdfFont`). Folio pins and
builds a separate copy, so in a monolithic packaged build both land in one executable - normally
a `LNK2005` waiting to happen.

It is not one, because `SlateSdfGenerator.cpp:34` does `#define MSDFGEN_PARENT_NAMESPACE
SlateMsdfgen` before including `msdfgen.cpp`. Verified in the mangled names rather than assumed:

```
Slate   generateMTSDF@msdfgen@SlateMsdfgen@@
Folio   generateMTSDF@msdfgen@@
```

`Module.SlateCore.2.cpp.obj` contains no unnamespaced `@msdfgen@@` symbol at all. So Folio needs
no parent namespace of its own and no patch.

**Re-check this on an engine upgrade.** If Slate ever drops that define, the two copies merge into
one namespace, and the failure is asymmetric: editor builds link SlateCore as a DLL and keep
working, while the packaged monolithic build fails. The fix if it happens is a
`patches/msdfgen/*.patch` adding the same macro on our side, which is what `folio_patch_command()`
exists for.

Two other pieces of that amalgam are worth knowing. It sets `MSDFGEN_DISABLE_VARIABLE_FONTS` and
`MSDFGEN_DISABLE_SVG` by default, and it compiles under `/GR-` with exceptions off - which is the
evidence that msdfgen needs neither RTTI nor exceptions, so `folio_set_msdfgen_options()` does not
have to fight Unreal's flags. Skia is enabled only `WITH_EDITOR`, and without it
`resolveShapeGeometry` is not compiled at all (the whole file is inside `#ifdef MSDFGEN_USE_SKIA`).
Folio does not build Skia either, so `MsdfGlyphGenerator` uses `normalize()` and
`orientContours()` instead; a font with self-intersecting contours will show artifacts there.

## Texture formats

**RmlUi's own texture loader reads uncompressed TGA and nothing else.** Not PNG. Every real
atlas therefore failed to load with `Only 24/32bit uncompressed TGAs are supported`, which
reads as a corrupt-file error rather than an unsupported-format one. `uipreview`'s render
interface decodes `.png` itself through lodepng, which it already links for screenshots, and
premultiplies on the way in because 6.x expects premultiplied texture data and lodepng
decodes straight alpha. Phase 3 needs the same thing from Unreal's texture path.

## Missing sprites

Most decorators resolve a value as a sprite name first and **silently** fall back to treating
it as a filename. Nothing is logged at the decorator layer; the only signal is
`TextureDatabase`'s `"Could not load texture"` warning when `RenderInterface::LoadTexture`
returns 0.

So `LoadTexture` never returns 0: on failure it returns a generated magenta checkerboard.
Verified that this **does** suppress RmlUi's own message, so the wording is ours.
`LoadTexture` runs **before layout**, so it cannot know what size the sprite will be drawn at.
The rendered size is captured instead from the compiled geometry's bounding box at draw time.

**The name arriving at `LoadTexture` is already joined against the document directory.** An
undeclared sprite `icon_sword` referenced from `/ui/examples/hud.rml` arrives as
`/ui/examples/icon_sword`, not as the bare name. So "contains a path separator, therefore it
was a file reference" is wrong, because every unresolved sprite looks like that. Classify on
the **file extension** instead: no extension means it was a sprite name, an extension means it
was a file or spritesheet `src` reference.

**Not every decorator reaches `LoadTexture`.** Confirmed by experiment:

| Decorator | Undeclared sprite behaviour |
|---|---|
| `image`, `tiled-horizontal`, `tiled-vertical`, `tiled-box` | Silent. Falls through to `LoadTexture`, caught by the placeholder. |
| `ninepatch` | Sprite-only. Never reaches `LoadTexture`, logs `Could not find sprite named '<name>' in ninepatch decorator.` plus `Decorator 'ninepatch' ... could not be instanced, declared at <file>:<line>`, and renders nothing at all. |
| `<img sprite="...">` | Not a decorator, and sprite-only in the same way. Logs `Could not find sprite '<name>' specified in img element <path>` and draws a white box at the element's size. |

The `img` wording differs from the ninepatch one (`sprite '<name>'` against
`sprite named '<name>'`), so both prefixes have to be matched when scraping the log. Neither
route reaches the render interface, so neither has a drawn size to report; for `img` the size
is recovered from the element's laid-out box after the document settles, which is why an
`img` entry in `ASSETS_WANTED.md` has a real size and a ninepatch entry is `0x0`.

So missing sprites have to be collected from two sources: the `LoadTexture` fallback, and the
ninepatch warning parsed out of `LogMessage`. The ninepatch route is the one that supplies a
file and line number.

## Data models built from JSON

`Rml::VariableDefinition` does support a fully dynamic model with no compile-time structs, and
`data-for` over an array of objects works. `BindCustomDataVariable` takes a `DataVariable`
directly, so `RegisterCustomDataVariableDefinition` is not needed as long as the definition
objects are kept alive by the caller.

**Every bound address needs a pointer that stays valid, not just the root.** RmlUi re-walks an
address from its *bound* variable on each update, so binding top-level keys as pointers into
one root JSON object and then reassigning that root wholesale leaves each bound pointer
dangling: nlohmann frees the sub-objects on assignment. The symptom is the whole model going
blank after a reload, with `struct has no member '<name>'` warnings for nested paths.

Each top-level key therefore gets its own heap slot (`map<string, unique_ptr<ordered_json>>`)
and a reload assigns through the slot. Everything below a bound slot is re-resolved per
update, so nested objects and arrays may change shape freely, including arrays changing
length.

A consequence: **keys that appear only after the model is constructed cannot be bound**, since
the constructor is gone. A reload that introduces a new top-level key needs a document rebuild.

`VariableDefinition::ReflectMemberNames()` takes no node pointer, so member names cannot be
reflected per node. The debugger's data-model viewer will not list JSON object members.

## Screenshots from a hidden window

`glReadPixels` against a window created with `GLFW_VISIBLE=false` was verified to produce a
capture **byte-identical** to the same scene in a visible window (0 of 1,228,800 bytes
differing). No FBO indirection is needed.

Capture has to happen after `RenderInterface_GL3::EndFrame` and before `glfwSwapBuffers`:
`EndFrame` is what resolves the renderer's layer framebuffers into the default framebuffer.

## Git LFS is not set up

`.gitattributes` marks `*.png` / `*.ttf` / `*.otf` as plain `binary`, not LFS, because
`git lfs install` has never been run on this machine and an undefined filter is worse than no
filter. Folio bundles no art or fonts, so nothing needs it yet. Before a repo starts
accumulating atlases, run `git lfs install` and switch those lines to
`filter=lfs diff=lfs merge=lfs -text`.

## Profiling

Folio reports to Unreal Insights, `stat folio`, and CSV captures. Capture with:

```
UnrealEditor.exe <project> -trace=default,counters,folio -statnamedevents
```

`folio` is a channel of its own so a capture can ask for it without everything else. In the
editor, `stat folio` shows the same timings live.

The scopes worth knowing, in the order they cost:

| Scope | What it covers |
| --- | --- |
| `Folio/GenerateGlyph` | One glyph's field, cold. Everything below is inside it. |
| `Folio/GenerateField` | msdfgen itself. **This is the expense**, around 2.8ms a glyph. |
| `Folio/ResolveOverlaps` | Skia unioning the outline. Around 0.04ms - 1.5% of the glyph. |
| `Folio/ReadGlyphCache` | Reading an instance's fields back from disk. |
| `Folio/PackGlyph`, `Folio/UploadGlyph` | Placing it in the atlas and sending it to the GPU. |
| `Folio/GenerateString`, `Folio/GetStringWidth` | What RmlUi calls per text element. Cold, these contain the generation above. |
| `Folio_Render`, `Folio_Update` | The canvas widget's paint and tick. |
| `Folio_CompileGeometry`, `Folio_DrawGeometry` | Per draw, on the game thread. |
| `Folio_ResolveLayers` | The layer stack's replay, on the render thread. |

Counters: `Folio/AtlasPages`, `Folio/AtlasMemory`, `Folio/AtlasGlyphs`, `Folio/FaceInstances`,
`Folio/GlyphsGenerated`, `Folio/GlyphsFromCache`. Memory is tagged for `-llm` under `Folio`.

**Do not read the generation cost as Skia's.** It is msdfgen's field generation; resolving
overlaps is a rounding error beside it. An earlier commit message says otherwise and is wrong.

FolioCore cannot use Unreal's profiling macros - it compiles into the preview tool too - so it
names scopes through a hook table in `FolioCore/FolioTrace.h` that the Unreal module forwards to
Insights. The preview tool implements the same hooks and totals them:

```
uipreview <doc> --font-report
```

Numbers there are inclusive, so a parent scope contains its children.

## Toolchain

**The MSVC toolset must match whatever UnrealBuildTool picks, and guessing it was wrong.**
This was originally pinned to 14.38.33130 on the assumption that UE 5.8 selects it. It does
not: on this machine UBT uses **14.50.35717**, from a Visual Studio 18 BuildTools install, and
RmlUi built with 14.38 was being linked into modules built with 14.50. It linked, which is
the dangerous part, because RmlUi's interface passes `std::string` and `std::vector` across
that boundary and a layout change would corrupt rather than fail.

Both build scripts now default to `-Toolset latest`, which is also UBT's default. When they
disagree, UBT's `Toolchain Information` line is the authority; pass the version explicitly.

`CMakePresets.json` no longer names a version either. Its toolset strategy is `external`, so
CMake never applied the value - it only ever read as documentation, and it documented a
toolset that is not installed.

A build tree caches `CMAKE_CXX_COMPILER` from its first configure, but `INCLUDE` comes from
whatever `VsEnv.ps1` resolves now. When those drift apart the STL fails the compiler out
rather than the mismatch being silent:

```
yvals_core.h(929): error C2338: static_assert failed:
    'error STL1001: Unexpected compiler version, expected MSVC 19.44 or newer.'
```

followed by a hundred errors inside `<type_traits>` and `<xutility>`. It looks like the
dependency that happened to compile first is broken. It is not - the fix is
`build.ps1 -Clean`.

`scripts/build_ue_libs.ps1` builds RmlUi for Unreal, separately from the preview tool. The
two need different settings: the engine's FreeType rather than our own, `/MD`, no RTTI, and
output staged into `Source/ThirdParty/RmlUiLibrary`. Those libraries are **not committed** -
they run to tens of megabytes and would re-enter history on every rebuild - so a fresh clone
runs the script once before building the plugin. `RmlUiLibrary.Build.cs` fails with that
instruction rather than a link error if they are missing.

**The engine's FreeType needs zlib and libPNG named explicitly.** `FreeType2.Build.cs`
declares them as its own private dependencies, which does not reach the module that actually
links, so `freetype.lib` arrives with 27 unresolved `png_*` and `inflate*` symbols. Adding
`zlib` and `UElibPNG` alongside `FreeType2` fixes it.

`RMLUI_FONT_ENGINE` is kept as a build parameter rather than hardcoded, because Phase 3 may
have to set it to `none` and supply a UE-backed font engine to avoid colliding with UE's own
copy of FreeType (`Engine/Source/ThirdParty/FreeType2`).

## Live Coding cannot introduce a global shader type

Adding `FFolioUIMsdfPS` and hot-patching it in killed the editor on the spot:

```
Assertion failed: !AreShaderTypesInitialized() [Shader.cpp:315]
Shader type was loaded too late, use ELoadingPhase::PostConfigInit on your module
```

The patch DLL links fine and the process dies while loading it, so there is nothing to read
except the assert. Global shader types are registered from static initialisers and the engine
seals that list long before a patch arrives; the advice in the message does not apply, because
the module already loads early enough on a normal start.

Editing the body of an existing shader is fine - shaders compile at runtime and the editor
picks up a changed `.usf` on its own. Adding, removing or renaming a `IMPLEMENT_GLOBAL_SHADER`
needs the editor closed and a full build.


## A fullscreen RDG pass does not use your vertex shader

Every layer effect in Unreal - outer box-shadow, `filter`, `backdrop-filter` - rendered as
nothing, and the cause was one line in `FolioBlur.usf`:

```hlsl
float4 MainPS(float4 SvPosition : SV_POSITION, float2 UV : TEXCOORD0)
```

`FPixelShaderUtils::AddFullscreenPass` supplies **the engine's** vertex shader, not the `MainVS`
sitting next to that function. Nothing writes `TEXCOORD0`, so every tap sampled the same
undefined coordinate, the blur returned a uniform value, and the pass overwrote the layer with
it. The layer had drawn correctly the whole time. UV comes from `SvPosition.xy * InvSize` now -
`InvSize` was already being set and never read, which was the clue.

Two things made this expensive to find. The blur runs on the *result* of the layer, so the
symptom points at the layer, not at the filter. And the failure is silent in every channel that
usually helps: no RHI validation error, no shader warning, correct pipeline state, correct
geometry, a dead vertex shader that still compiles and still gets an `IMPLEMENT_GLOBAL_SHADER`.

**What actually settled it was RenderDoc, and specifically reading the render target between
passes.** After `FolioLayer` the target held 15188 non-zero pixels; after `FolioBlur` it held
zero. That one comparison named the culprit in a way that hours of reasoning about PSOs,
scissors, volatile buffers and RDG culling did not - all of which were checked and all of which
were fine.

Capturing it needs care: the Folio window is its own `SWindow` with its own swapchain, so
`RenderDoc.CaptureFrame` (which needs `-AttachRenderDoc` on the command line) captures the main
editor viewport and not the document. Attach from the RenderDoc UI with **capture all activity**
and make the window repaint. The pass is marked `FolioLayer` in the event browser; confirm the
right capture by looking for a render target the size of the document window.

## Folio bundles typefaces, reversing the brief

`FOLIO_BRIEF.md` says "Font registration defaults to Unreal's Roboto ... Do not bundle a
typeface." Folio ships Inter and RobotoMono, both variable, under `fonts/`, and that was a
deliberate reversal agreed when the distance-field engine went in.

The reason is that the engine's Roboto is a static face. Variable-font support is most of the
value here: `font-weight: 450` is a real instance rather than a snap to the nearest cut, and
`opsz` is driven from the requested size automatically. Neither is expressible with a static
face, so shipping one that has axes is what makes the feature reachable by default rather than
only after a game supplies its own faces.

Resolution order is unchanged in spirit and is the same in both renderers: the game's own
`ui/fonts/` first, then the faces Folio ships, then the engine's Roboto as a last resort. A game
that drops faces in `ui/fonts/` still wins outright, which was the part of the brief's intent
that mattered.

The first face registered becomes the fallback. Without one, a document naming a family nobody
registered renders nothing at all, which reads as a layout bug and says nothing about the cause.

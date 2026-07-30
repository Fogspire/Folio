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

**Target: all of them.** Everything except the non-blur filters and `mask-image` is in. Status
table at the end of this section.

The Unreal render interface implements the complete RmlUi 6.x optional
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
| Clip masks | Done. A real stencil on the layer path, so `SetInverse` is exact |
| Render layers | Done. Render targets recorded on the game thread and replayed with RHI on the render thread, one pass per layer. Pooled by size |
| `filter: blur` | Done. A separable gaussian global shader over the layer target |
| `backdrop-filter: blur` | Done, via a Slate post-process pass, so the 3D scene behind a panel blurs with it. The region is a rectangle, so a rounded corner blurs square |
| `box-shadow` | Done |
| Text, font effects, vector sprites | Done. Distance fields through a UI-domain material |
| Other filters, `mask-image` | Accepted and ignored. The layer machinery is there; only the shaders are missing |
| `decorator: shader(...)` | Done. additive, scanlines and sweep, rasterised rather than shaded |

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

**A distance field atlas is not colour, and must not be treated as one.** `SRGB = false`, and
premultiplying is wrong. Both mistakes were made and both are quiet:

- sRGB on an atlas applies a transfer curve to numbers that are distances, so the field's zero
  crossing moves and every glyph comes out the wrong weight.
- Premultiplying multiplies RGB by alpha. In an MTSDF, alpha is the *true signed distance*, not
  coverage. The loaders did this to the vector atlas at first and the art eroded to blobs.

The renderer detects a field atlas *before* the premultiply step for exactly this reason.
Bilinear, clamped, no mips, never streamed.

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
whatever `vsenv.sh` resolves now. When those drift apart the STL fails the compiler out
rather than the mismatch being silent:

```
yvals_core.h(929): error C2338: static_assert failed:
    'error STL1001: Unexpected compiler version, expected MSVC 19.44 or newer.'
```

followed by a hundred errors inside `<type_traits>` and `<xutility>`. It looks like the
dependency that happened to compile first is broken. It is not - the fix is
`build.sh --clean`.

`scripts/build_ue_libs.sh` builds RmlUi for Unreal, separately from the preview tool. The
two need different settings: the engine's FreeType rather than our own, `/MD`, no RTTI, and
output staged into `Source/ThirdParty/RmlUiLibrary`. Those libraries are **not committed** -
they run to tens of megabytes and would re-enter history on every rebuild - so a fresh clone
runs the script once before building the plugin. `RmlUiLibrary.Build.cs` fails with that
instruction rather than a link error if they are missing.

**The engine's FreeType needs zlib and libPNG named explicitly.** `FreeType2.Build.cs`
declares them as its own private dependencies, which does not reach the module that actually
links, so `freetype.lib` arrives with 27 unresolved `png_*` and `inflate*` symbols. Adding
`zlib` and `UElibPNG` alongside `FreeType2` fixes it.

**`RMLUI_FONT_ENGINE` is `none` in both builds.** Folio supplies its own
`Rml::FontEngineInterface`, so RmlUi's FreeType engine is not compiled at all - which also
sidesteps colliding with the engine's own copy of FreeType
(`Engine/Source/ThirdParty/FreeType2`).

The consequence is worth knowing before it happens: `Rml::Initialise()` **fails hard** with "No
font engine interface set!" unless `SetFontEngineInterface` ran first (`Core.cpp:109-118`). Both
`FolioModule::StartupModule` and `uipreview`'s `App` install theirs before initialising, and the
failure is loud, but a new host that forgets will not get a subtle degradation.

The debugger's font loads through the **memory** `LoadFontFace` overload under the family
`rmlui-debugger-font`. A font engine that only implements the file overload gives a debugger
with no text at all, which reads as a rendering bug.

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

## The font engine must be released before the render interface

`uipreview` segfaulted roughly one cold-cache run in thirty, always after the screenshot was
already on disk, never in the same document twice. `App::~App` shut RmlUi down, then called
`Backend::Shutdown()`, which destroys the render interface. The font engine is a member, so it
was destroyed *after* the destructor body - and `FMsdfAtlas::~FMsdfAtlas` releases every atlas
page through `IMsdfTextureSink`, which is that render interface. A use-after-free on the sink's
vtable, on every single run, that happened to survive most of the time because freed memory
usually still holds what it held before.

`font_engine.reset()` now runs in the destructor body, after `Rml::Shutdown` (which still reads
through the engine while releasing font resources) and before the backend goes.

The general rule this is an instance of: **anything holding an `IMsdfTextureSink` has to die
before the object implementing it.** Member declaration order will not arrange that on its own
when part of the teardown is explicit calls in a destructor body.

**Use `build.sh --asan` for any intermittent crash here.** It named this one on the first run,
with both stacks - the free and the later read - after an afternoon of bisecting had only
established that it needed a cold cache. It builds into `build/win64-asan` but writes its
binaries to the same `bin/win64`, so rebuild without the flag afterwards, and run it from a
shell with the MSVC environment imported or the sanitizer runtime will not resolve.

## Advances changed when the text engine did

Folio's advances come from FreeType's unhinted `linearHoriAdvance`, scaled linearly from design
units. Hinted advances do not scale linearly - that is the whole point of hinting - and a
non-linear advance cannot serve one size-independent atlas, so this is deliberate rather than an
oversight.

It does mean every existing screen re-flows slightly against a hinted rasteriser, and that a
line break is not stable across dp ratios: an advance is a whole number of pixels at each size,
so doubling the ratio halves the relative rounding error and words move between lines.

`uipreview --metrics-report` and `scripts/verify.sh --baseline` exist to make that visible
rather than discovered. Compare like for like - same size, same dp ratio. A break that moves
after an engine change is usually correct and still has to be looked at.

## Baking is a build step, not a runtime one

`msdfbake` runs offline and writes into the game's content:

- `msdfbake font` writes `.folioatlas` caches. They are an optimisation - a missing one is
  generated at runtime, costing about 0.2-0.5 ms per glyph - so packaging without them works
  and merely stutters on first use of a face.
- `msdfbake vector` writes the atlas PNG, its sidecar and a `@spritesheet` block. These are
  **not** optional. Nothing generates a vector atlas at runtime, so an unbaked SVG is a missing
  sprite in the packaged build.

So the vector bake belongs in the content pipeline ahead of cook, next to whatever packs the
raster atlases, and its outputs are committed like any other art. Re-run it after editing an
SVG. RmlUi caches textures by path, so a re-baked atlas at the same path renders stale in the
editor until it restarts.

## Engine upgrade checklist

Things that read engine internals and would fail quietly rather than loudly if the engine moved:

- **`ChooseMaterialShaderTypes`** - the UI-domain material reaches the distance field shader
  because `ElementBatcher.cpp` tags custom-verts batches `ESlateShader::Custom` and
  `SlateRHIRenderingPolicy.cpp` maps that to `TSlateMaterialShaderPS<ESlateShader::Custom>`. If
  that mapping changes, text loses its shader and falls back to something that draws the raw
  field as colour.
- **`FSlateResourceHandle`'s private constructor**, friended only to
  `FSlateShaderResourceManager`. Only `GetResourceHandle(Brush)` yields one; anything else that
  looks like it should work does not compile, and the workaround people reach for is a
  reinterpret cast.
- **`FSlateBaseUTextureResource::AccessRHIResource`** calls `FApp::GetCurrentTime()` from the
  render thread, which ensures on `IsInGameThread()`. Folio uses
  `FSlateDynamicImageBrush::CreateWithImageData` to get a native `FSlateDynamicTextureResource`
  and avoid that branch entirely.
- **Slate's own MTSDF maths** in `SlateElementPixelShader.usf`. Folio's spread term is taken
  from it deliberately; if Slate's changes, the two stop agreeing about what a field means.
- **msdfgen is vendored by SlateCore too.** Both copies link into a monolithic build, so ours
  compiles under `MSDFGEN_PARENT_NAMESPACE` and the macro must match between the library build
  and `MsdfgenLibrary.Build.cs`. Mismatched, it links in the editor and fails at packaging.

## Releasing a texture can delete a live glyph atlas

`Rml::ReleaseTextures()` releases **callback** textures as well as file ones
(`TextureDatabase.cpp`), and a distance-field page is handed to RmlUi as a callback texture. So
a hot reload, which calls it to pick up changed art, arrives at `ReleaseTexture` holding the
font engine's own atlas page.

Freeing it there deletes a texture the font engine still owns and still writes new glyphs into.
RmlUi then re-runs the callback and gets the same handle back, now naming nothing. In the
preview tool that made every glyph disappear on the next `.rcss` edit; the screenshot went from
82 KB to 3.4 KB.

Both renderers now track which field pages came from a file, and release only those. A glyph
atlas belongs to the font engine and goes at shutdown through `ReleaseAtlasHandle`.

`uipreview --reload-textures` renders, releases everything, renders again and captures.
`verify.sh` compares that against an ordinary capture and fails on any difference, which is the
only cheap way to catch this class of bug: it needs a reload to happen, and a reload is
something you normally only do by hand.

## An effect cannot reach as far as the field does

`outline(20px)` on 40dp text drew each glyph as a solid rectangle of colour, quad edges and all,
with the text as a row of coloured boxes. It looked like the effect quads were leaking into each
other. They were not.

The outline branch is `saturate((sd - (Threshold - Offset)) * Spread + 0.5)`. Clamping the offset
to exactly half the field's range put `Threshold - Offset` at zero - and zero is where the field
saturates. Every texel beyond `PxRange/2` holds zero, whether it is one texel past the edge or a
whole em away, so every one of them evaluated as sitting exactly on the outline and came out at
half coverage. The whole padded quad filled, and the quads abut.

The cap is now `(PxRange/2 - 1) / PxPerEm`, which is 0.104em at the defaults instead of 0.125em.
One texel of field is held back for the effect's own edge to fade out in. `MaxEffectEm()` is
therefore no longer half the range, and the em-to-field-units conversion had to stop deriving the
range from it - they are separate numbers now.

The glow and blur branch was already safe, because its lower bound is clamped to zero for a
different reason, but it had its own edge: a glow with no blur gives `smoothstep` a zero-width
ramp, which is a division by zero. Its upper bound now has a floor.

`Tools/uipreview/tests/effect_edges.rml` is every effect asked for well past the cap. It is
expected to warn and to exit 5 under `--strict`; what it must never show is a rectangle or a seam
between adjacent glyphs.

## A layer stack has no height of its own

Every sprite in a `--layers` bake is `position: absolute`, so none of them contributes to the
container's height. Give the container a width and nothing at all renders, which reads as the
bake being broken rather than as a collapsed box.

RCSS has no `aspect-ratio` and no `calc()`, so nothing in a stylesheet can turn a width into the
right height. The only thing in RmlUi that carries a ratio is an image element's intrinsic size.

So the baker emits a `<name>_frame` sprite per drawing, holding the drawing's proportions, and a
`.<name>-frame` rule that is `visibility: hidden` - it keeps its box and never draws. A hidden
`<img>` on it is the first child of the stack, and the width alone is then enough.

The frame rectangle is **not packed and owns no atlas area**. A sprite rectangle is only
coordinates, and nothing ever samples this one, so it may sit over whatever else is there. It is
written as `0px 0px W H` scaled to a longest side of 512, which keeps the ratio error under a
fifth of a percent and cannot exceed a page.

`Tools/uipreview/tests/vector_stack.rml` covers it, with the container behind the art in magenta:
the drawing's own background layer must cover it exactly at every size.

## backdrop-filter is a pair, and the second half is the one that matters

`outside-blur` inverts a backdrop filter: the element becomes a hole and everything else is
blurred, which is what a modal scrim wants.

RmlUi issues a backdrop filter as two composites. The first reads what is already on screen,
runs the filters, and writes the result into a scratch layer. The second puts that layer back,
clipped to the element. Inverting the first achieves nothing - it only chooses which pixels get
blurred, and the second still stamps the result inside the element. The inversion belongs to the
second, which both renderers already defer to for their own reasons.

The first pass still needs changing, just differently: it is clipped to the element, so the
scratch layer only holds blurred pixels there. The bands the second pass draws are all outside
that, and would read pixels nothing had written. So the first pass runs unscissored.

Four bands - above, below, left, right - because a scissor is a rectangle and there is no way to
cut one out of another. Each composites the whole layer again rather than a slice, so every band
reads the same pixels and they meet without a seam. The corollary is that the hole is a
rectangle: a rounded dialog cuts a square one.

In Unreal it is four `MakePostProcessBlur` elements instead of one, and the clip has to come off
the stack first - it is the element's own scissor, and every band is outside it.

The filter is registered through `Factory::RegisterFilterInstancer` from `FolioFilters.h`, which
is header-inline for the same reason the font effect instancers are: an instancer's constructor
calls `RegisterProperty`, which resolves parsers through the `StyleSheetSpecification` that only
one module's `Rml::Initialise` populated.

## Stroking by piling up pieces does not survive a real drawing

A stroke has to become area, because a field has no notion of a line. The first stroker did that
the easy way: a rectangle per segment, a disc per vertex, and Skia's path operations to union the
pile. Simple, and correct on everything simple.

It failed on the drawings that matter. A ragged path - 64 points, ~169 after flattening, turning
sharply every few units - produced hundreds of overlapping contours, and the union came out
**solid**. A `fill:none` outline baked as a filled silhouette and hid the shape it was drawn
over. Diagnosing it cost an afternoon and produced three correct-but-irrelevant answers along the
way: stroke width was right, winding was consistent, and a closed stroked square baked perfectly.

The stroker now walks each contour once and emits one offset polyline per side, with the joins
put in: miter with its limit, round, or bevel, read from the SVG rather than forced. A closed
path gives two loops wound against each other, which is a ring under non-zero fill. An open path
gives one loop - up one side, round the cap, back down the other - with butt, round or square
caps. Two contours instead of hundreds, and the bake is faster for it.

On the inside of a corner the two offsets cross. That self-intersection is left for the path
operations to resolve rather than special-cased, which is what they are there for.

The cap arcs went the wrong way round first: taken the short way they pass through the stroke and
bite a crescent out of its end. The direction has to be the one that goes through the end point
plus the outward tangent.

`Tools/uipreview/tests/vector_strokes.rml` is one of each case, at two sizes.

## Font effects all drew behind the glyph, whatever they asked for

`Rml::FontEffect` lets an effect choose a side with `GetLayer()`, Back or Front. Every effect
RmlUi ships picks Back, and so did all four of ours, so `GenerateString` drawing the whole effect
set before the glyph went unnoticed for as long as nothing wanted the other side.

`fill-image` does. It is a front layer by construction - the glyph drawn again at the same
coverage with a pattern multiplied in - and the base pass painted straight over it. The symptom
was a red fringe: only where the glyph's own antialiasing let it through did any of the front
layer survive.

Back layers, then the glyph, then front layers.

## Textured text needs a second sampler, and that means editing the material

The pattern is a second texture multiplied into the resolved coverage. `FolioMsdfShade` grew
`Fill`, `FillSampler`, `FillUV` and `FillStrength`, which means `M_FolioMSDF`'s Custom node had
to grow three inputs to match - a binary asset, so it cannot be edited from a text diff.

Done headlessly, and repeatable:

```
UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script=<file> -unattended -nosplash
```

with `PythonScriptPlugin` temporarily added to the project's plugin list. It does not belong in
`Folio.uplugin`: it is an authoring tool used once, not something every game on Folio should have
to enable. The script is idempotent - it checks for the `Fill` input and exits if it is already
there.

`print` from a commandlet only reaches the console with `-stdout -FullStdOutLogOutput`. Without
those the script runs silently and looks like it did nothing.

**Where the pattern is read from is the host's job, not the field maths.** Unreal computes the UV
from `Parameters.SvPosition` in the Custom node; the preview tool from `gl_FragCoord`, flipped,
since GL puts the origin at the bottom. Both are screen space, so a pattern is continuous across
a whole run instead of restarting on each glyph quad. `check_shader_parity.sh` skips a block
fenced with `FOLIO_MSDF_HOST_BEGIN`/`END` for exactly this - the two renderers are expected to
differ there, and only there.

## RmlUi's visual test suite, and what its numbers mean

`scripts/visual_tests.sh` runs the 109 documents in `Tests/Data/VisualTests` from the fetched
RmlUi tree through uipreview. They cover layout, floats, flexbox, tables, transforms, filters,
clip masks, box-shadow and text, and they exercise the render interface far harder than anything
hand-written here. `verify.sh --visual-tests` runs them; it is off by default because they take
longer than the whole rest of the harness.

All 109 render. The suite still reports 197 errors and 448 warnings, and almost none of that is
ours:

- **180 errors across 89 documents are `<link>` tags with no `type`.** The suite uses them to
  carry metadata - a link to the W3C test it came from, a reference image. RmlUi's own parser
  rejects them; their harness ignores the log.
- **17 documents link `../Tests/Data/style.rcss`**, which is wrong relative to the document
  wherever it is resolved from. Those seventeen render unstyled, which is where 430 of the 448
  warnings come from - every element then has no `font-family`.
- One warning is ours, and correct: a test asks for a font effect wider than the field describes
  and gets told it was clamped.

So the bar is not zero. The baseline in `Tools/uipreview/tests/visual_baseline.json` records what
each document does today, and the script fails on any **increase**. A renderer regression moves
those numbers; a document we never handled stays where it is and is visible in the file.

Two things the script fixes up in the build tree, because uipreview has one content root by
design and their harness has several search paths: it copies `Samples/assets/*.tga` under
`Tests/Data/assets/` so the image tests resolve their art, and it registers the four LatoLatin
faces the suite's stylesheet names. A clean rebuild undoes both and the script redoes them.

## The Phase 3 gate, and how to re-run it

`folio.Gate [document] [seconds]` is the gate as a command rather than a description. It puts a
plain Slate widget in the viewport at order 50, the document at 100, and another widget at 200 -
the orders game UI actually uses - waits for the game to be up, then screenshots.

```
UnrealEditor.exe <project>.uproject -game -windowed -resx=1280 -resy=720 -nosplash \
    -ExecCmds="folio.Gate ui/examples/panel.rml 6"
```

The image lands in `Saved/Screenshots/WindowsEditor/FolioGate.png`. Passing means the document
renders, the order-200 widget covers its top edge, and the order-50 widget is hidden behind it.

**The command has to poll.** `-ExecCmds` runs during engine init, long before there is a game
viewport to put anything in, and a level load clears viewport widgets anyway - so the obvious
version ran, found nothing, and logged nothing. The first attempt at this screenshotted the
game's own loading screen. It now waits on `GetWorld()->HasBegunPlay()` and then gives the
document a few seconds to load its stylesheet, generate its glyphs and draw.

Verified with `ui/examples/panel.rml`: borders, radii, drop shadows, a vertical gradient, a
button and distance-field text, all in a running game, composited in the right place.


## Decorators paint in reverse source order

`decorator: linear-gradient(...), shader("additive #ff5028")` draws nothing you can see. Swap the
two and the effect appears. Comma separated decorators are painted **last to first**, so the one
named first ends up on top - the same way CSS `background-image` layers stack, and the opposite
of the reading that feels natural.

It cost a while on the shader decorator, because "an effect listed after the background does
nothing" looks exactly like "the effect is not implemented". `UI_CONVENTIONS.md` said the wrong
thing about this and now says the right one.

## The shader decorator, and why nothing is shaded

`decorator: shader("...")` is RmlUi's seam for a backend's own fragment shader: the string is
passed through untouched to `CompileShader`. Slate has no equivalent seam through
`MakeCustomVerts`, and none of the effects wanted here needs one - each is a pure function of
position over the element's box. So they are evaluated once into a small texture, in FolioCore,
and both renderers draw the same picture from the same code. A pair of hand-written shaders would
have drifted.

**Additive blending needs no blend state at all.** RmlUi composites premultiplied, `src + dst *
(1 - a)`, so a colour written with zero alpha adds to what is underneath. That was the thing the
brief called unreachable from RCSS, and it turns out to cost one line in the rasteriser.

The trade is that the set is fixed: `additive`, `scanlines`, `sweep`. A name Folio does not know
warns once and draws nothing, rather than pretending. Adding another is a function in
`FolioShaderEffects.cpp` and nothing else - no shader, no material, no per-renderer work.


## clip-path, honoured rather than dropped

A clip is an intersection and a field carries one silhouette, so the first read was that
`clip-path` could not be expressed and had to be flattened in the drawing program. That was wrong
about the bake: Skia's path operations are already vendored for unioning a glyph's overlapping
contours, and an intersection is the same call with a different operator.

What nanosvg does is the actual obstacle, and it does two things:

- It **discards the relationship**. So the baker reads the SVG itself for `<clipPath id="A">` and
  `<g clip-path="url(#A)">`, and maps shape ids to the clip that applies to them. Only the
  relationship - the geometry comes back from nanosvg with every transform already applied.
- It **emits the clip path's own geometry as an ordinary visible shape**. SVG never renders
  `<clipPath>` contents. In the test art the exporter had duplicated the cloak into the clip
  path, so that duplicate baked as a solid black layer on top of everything: the "hard black
  cape" that looked like a stroke bug for a long time. It is skipped now.

A clipped shape is built on its own, intersected, and only then added to its colour's layer.
Intersecting the layer would clip everything already painted into it.

## Rendering an SVG for comparison

Reasoning about a bake from layer numbers does not converge. Edge renders SVG properly and is on
every Windows box:

```
msedge.exe --headless=new --disable-gpu --screenshot=out.png --window-size=760,800 file:///page.html
```

with a one-line HTML wrapper holding an `<img src="file:///drawing.svg">`, because the headless
screenshot of an SVG loaded directly comes out empty.

For finding which layer is at fault, render each one alone: a document with one `<img>` per
`-ink` class, tinted, laid out in a grid. That is what showed the clip working (the clipped
layers were correctly confined) while the fills were not.


## Three keyboard schemes at once, without breaking typing

The phase gate asks for a menu navigable by arrows, WASD and IJKL simultaneously. The catch is
that W is also a letter, and a text field has to keep receiving it.

The order settles it. `OnKeyDown` asks for the *literal* key first - `ToRmlKey`, which knows
arrows, Enter, Escape, Tab and the editing keys - and only if that comes back unknown does it ask
`ToNavigationKey`, which knows the gamepad and the letter schemes. That second question is skipped
entirely while an `input` or `textarea` has focus.

So the two maps are deliberately separate. Folding the letters into `ToRmlKey` would make them
directions everywhere, including inside a text field.

## A stick is a position, not an event

Slate reports an analog axis every frame it is off centre, so forwarding it directly either does
nothing or navigates every frame. `FAnalogNavigator` records the axes in the input handler and
turns them into presses in `Tick`, where there is a clock: past a 0.5 deadzone it emits once,
waits 0.4s, then repeats every 0.12s - a keyboard's auto-repeat shape, and the timings the brief
names.

Two details worth keeping. The larger axis wins, so a diagonal push means one direction rather
than two, and which one no longer depends on frame timing. And changing direction without
returning to centre counts as a new press, because that is what it feels like to the player.

The navigator is reset on focus loss: a stick held while focus moves elsewhere would otherwise
keep repeating into a document that is no longer in front.

Navigation keys are sent down **and up** together. RmlUi navigates on the press, but an element
left believing a key is still held misbehaves the next time that key arrives.

## A document is updated and rendered every frame, changed or not

`SFolioCanvas::Tick` calls `Context::Update()` and `OnPaint` calls `Context::Render()` unconditionally,
so an idle document costs a full layout traversal, a full render traversal and a fresh set of Slate
draw elements every frame. UMG under an invalidation panel skips all of it. This is the one known
structural disadvantage against UMG and it is written up in `docs/FOLIO_BRIEF.md` Phase 6.

The reason it has not been fixed casually: RmlUi exposes no dirty signal. There is no context-level
flag, `ElementDocument::IsLayoutDirty()` is private, and active animations are private per-element. So
skipping work needs either a patch that exposes a query or conservative tracking on our side, and the
failure mode - a document that stops updating - is worse than the cost. The patch is the expected
route: we pin upstream `mikke89/RmlUi` and patch at fetch time rather than maintaining a fork, and one
patch is already carried, so widening `IsLayoutDirty` costs a re-base across an RmlUi bump and nothing
else.

Do not "optimise" this by only updating on input. Caret blink, `transition`, `animation`, `:hover`
settling, data model updates from gameplay and `@media` re-evaluation all change with no input.

## Dev tooling is compiled out of Shipping, except one UCLASS

`FolioConsoleCommands.cpp` and `FolioDebugger.cpp` are wrapped in `#if !UE_BUILD_SHIPPING`, so
`folio.Show`, `folio.ShowWindow`, `folio.Gate`, `folio.NavTest`, `folio.MvvmTest`, `folio.Debugger`,
`folio.MouseMode`, `folio.ReloadTextures` and `folio.Nav.Bind*` do not exist in a retail build. The `folio.Nav.*` console *variables* are not affected - they are runtime
settings and live in `FolioNavigation.cpp`.

`UFolioBindTest` stays compiled in Shipping deliberately. A `UCLASS` inside a preprocessor block is not
something UnrealHeaderTool handles: it generates the reflection code regardless and the link then
fails. An unreferenced class registration costs nothing, so it is the lesser evil. If it ever needs to
go, the supported route is a separate module with `"TargetConfigurationDenyList": ["Shipping"]` in
`Folio.uplugin`, which also means fixing the public headers that currently include private ones
(`SFolioCanvas.h` includes `FolioNavigation.h`) - no external module can compile against them today.

Verify with a string search, and remember `TEXT()` is UTF-16: searching a binary for ASCII
`folio.NavTest` finds nothing either way, which reads as a pass when it is not a test at all.

## The RmlUi debugger lives inside the context it inspects

`folio.Debugger` attaches `Rml::Debugger` to a canvas. Three things about it are not obvious:

**There is one debugger per process, and it is hosted by a context.** `Debugger::Initialise(ctx)` puts
its own documents into `ctx`, so it draws through our render interface and receives mouse through the
canvas's normal forwarding. Inspecting a second document means `Shutdown()` then `Initialise()` on the
other context, never a second `Initialise`.

**RmlUi releases the debugger when its host context is destroyed** and tells us nothing. So the stored
host pointer can outlive the debugger, and every entry point in `FolioDebugger.cpp` re-checks the canvas
against `SFolioCanvas::GetLiveCanvases()` before touching the API. Context lifetime equals canvas
lifetime, which is what makes that check exact.

**Its font comes through the *memory* `LoadFontFace` overload** as family `rmlui-debugger-font`, not
through a file. A custom font engine that only implements the path overload compiles, runs, and renders
the debugger's chrome with no text in it at all - a symptom that points nowhere near the cause.
`FFolioFontEngine` implements both; verified by screenshotting `uipreview --debug`.

It also needs a cursor to be usable, which a game that has captured the mouse will not have.
`folio.MouseMode on` sets `FInputModeGameAndUI` with the cursor shown and no viewport lock.

## Hot reload: what decides the granularity, and why the code is duplicated

`folio.HotReload` (default on, editor only) watches `<project>/ui` and `<plugin>/rcss` with
`IDirectoryWatcher` and reloads open documents. Three things are load-bearing.

**The dependency list is what RmlUi opened, not a parse of `@import`.** `FFolioFileInterface` records
every path it successfully opened, so the list covers the document, every imported and linked
stylesheet, every font and every texture, and it cannot drift from RmlUi's own resolution rules. It
only grows, so a file that is no longer referenced costs a wasted reload rather than a missed one.

**Granularity is not an optimisation.** Rebuilding a document loses everything the DOM held, so an
`.rcss` change reloads stylesheets only and leaves focus, scroll and the data model alone. `.rml`
rebuilds and puts focus and scroll back via `FolioReload.cpp`. Anything else opened is art:
`ReloadChangedTextures` by name plus `ReleaseCompiledGeometry`, deliberately not
`Rml::ReleaseTextures`, so the distance field pages are not dropped with it.

**`FolioReload.cpp` duplicates the preview tool's `Reload.cpp`/`ElementPath.cpp` on purpose.** RmlUi
is a static library linked into both the Folio and FolioCore modules, so each has its own copy of
RmlUi's globals. `Rml::Factory::ClearStyleSheetCache` and `Rml::ReleaseTextures` called from a
FolioCore translation unit clear FolioCore's caches, which are empty, and the reload silently does
nothing. Anything touching RmlUi's globals has to be compiled into the module that owns the context.
Do not "de-duplicate" this into FolioCore.

Two more traps worth knowing:

- **Editors save by writing a temporary file and renaming over the target**, so for a few
  milliseconds after a notification the file is locked or zero length, and every other save looks
  like a parse error. `FFolioFileInterface::Open` retries for 250 ms under `WITH_EDITOR`, but only
  when the file already exists, so a genuinely missing one still fails immediately.
- **A single save arrives as several notifications** and an atlas re-bake touches many files, so
  changes accumulate and apply once on the next tick rather than per notification.

**Mock data does not hot reload.** `ui/mock/<model>.json` is read with `FFileHelper`, not through the
file interface, so it is not in the dependency list and a change is ignored. Making it work needs a
change to `FFolioDataModel`: it binds the address of the parsed `FJsonValue` node, so a re-parse would
leave RmlUi holding a freed pointer. The fix is to bind a stable slot that the definitions dereference,
which is what the preview tool does. Reopen the document for now.

## M_FolioMSDF is not cooked unless the project asks for it

`FFolioRenderInterface` loads the distance field material with
`LoadObject<UMaterialInterface>(nullptr, TEXT("/Folio/M_FolioMSDF.M_FolioMSDF"))`. Nothing *references*
the asset, so the cooker never sees it, so it is absent from every package and all text renders with
no shader. The symptom is one log line - `could not load /Folio/M_FolioMSDF; text has no shader to
resolve its fields` - and otherwise looks like a text rendering bug.

Every project using Folio needs this in `DefaultGame.ini`:

```ini
+DirectoriesToAlwaysCook=(Path="/Folio")
```

There is no way for the plugin to declare it: `"CanContainContent": true` makes the content *mount*,
not cook. A `UDeveloperSettings` holding a `TSoftObjectPtr` would not help either, since a config soft
reference is not a cook reference without asset manager rules. Confirmed empirically - the material was
missing from the package until the entry was added, and present afterwards.

## Staging: DirectoriesToAlwaysStageAsUFS is relative to Content, not the project

Folio's files are not assets, so they reach a package only by being staged, and getting the path base
wrong is silent: the entry is accepted, UAT logs `Unable to find directory ... for staging` as a
warning nobody reads, and every screen is empty in a build that worked in the editor.

`StageAdditionalDirectoriesFromConfig` in `CopyBuildToStagingDirectory.Automation.cs` combines each
`(Path=...)` against **`ProjectContentRoot`** and stages into `StageContentRoot`
(`<RelativeProjectRootForStage>/Content`). Folio resolves against `FPaths::ProjectDir()`, which is one
level up. So every entry starts with `../`:

```ini
+DirectoriesToAlwaysStageAsUFS=(Path="../ui")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/rcss")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/fonts")
```

`..` is safe here: `StagedFileSystemReference`'s constructor **collapses** parent references rather
than rejecting them, and only throws if the result escapes the staging root. `Kaboom/Content/../ui`
becomes `Kaboom/ui`, which is what `FPaths::ProjectDir()` resolves to at runtime.

**There is no way to exclude a subdirectory of one you have staged.** `FindFilesToStage` recurses
everything except `RestrictedFolderNames` (NoRedist and friends) and drops only `.uasset`, `.umap`,
`.po` and `.archive` by extension. That is why Folio's dev documents were moved from `rcss/dev/` to
`dev/`, with `/folio/dev/` mounted onto it in `FFolioFileInterface::Resolve` so every existing path
still reads the same.

Verify from the manifest rather than by launching:
`Engine/Programs/AutomationTool/Saved/Logs/FinalCopyWin64_UFSFiles.txt`, or
`UnrealPak.exe <pak> -List`. Restaging alone is fast - `BuildCookRun -skipbuild -skipcook -stage -pak`
took 3 seconds against an existing cook, so there is no reason to package fully to check this.

**Generated font fields are not staged.** `FontCacheDir` is `<Project>/Saved/Folio/Fields`, and Saved
is not part of a package, so a packaged build regenerates fields on first use. That is the Latin-1
prewarm cost per face at startup, not per glyph, but a prebaked read-only cache staged alongside the
faces is the obvious improvement.

## -uidev: loose files ahead of the pak

`-uidev` (or `-uidev=<path>`) makes `FFolioFileInterface` check an override directory before the
package, so UI can be changed on a device - a console, a Steam Deck - without cooking again. The
directory mirrors the project, which is also the staged layout, so `uidev/ui/screens/hud.rml` overrides
`ui/screens/hud.rml`. A file that is not there falls through, so a directory holding one document
works. Compiled out of Shipping.

Bare `-uidev` resolves to `FPaths::ProjectDir()/uidev`, which in a staged build is
`<Staged>/<Project>/uidev`, beside the `ui` tree it shadows. Explicitly **not**
`FPlatformProcess::BaseDir()`: once staged that is `<Project>/Binaries/<Platform>`, which is nowhere
anyone would think to drop files.

The override is logged as a **warning** at startup on purpose. A stale file there presents as the game
ignoring an edit, and nothing in that symptom points at an override directory.

## Commands out of a document: BindEventCallback over reflection

`data-event-click="Confirm"` reaches game code because `FFolioObjectModel` binds every
`UFUNCTION(BlueprintCallable)` on the bound source as an RmlUi data event callback, the mirror image of
how it binds properties as values. `data-event-click="Damage(0.25, 'a button')"` works too: RmlUi hands
the callback a `VariantList` and the arguments are written into the call frame in declaration order.

Rules for a function to be reachable, and they are checked at mount rather than at click:

- `BlueprintCallable`, which is the same "the author marked this as a surface" test the properties use
  with `CPF_BlueprintVisible | CPF_Edit`.
- **No return value.** A data expression discards it, so a function with a result is almost always a
  query that was meant to be bound as a value.
- Parameters are `bool`, a number, `FString` or `FName`, **by value**. An out or reference parameter has
  nowhere to go: RmlUi passes variants in and reads nothing back.

Anything rejected is logged by name, but **only if the function is declared on the source's own class**.
Every base class contributes callable functions nobody meant as UI commands, and naming those would bury
the one that matters.

`ProcessEvent` needs one contiguous frame holding every parameter at its reflected offset, and the frame
must be constructed and destroyed by the properties themselves rather than memset. An `FString`
parameter left as zeroed memory is a string that frees a pointer it never owned. Arguments the
expression did not supply keep whatever the property initialised to, so a command with an optional
argument still works from `data-event-click="Name"`.

## A data model must exist before the document that reads it loads

RmlUi resolves a document's bindings as it loads and gives no way to attach a model afterwards. It also
refuses a second model under a name it already has. Together those two facts caused a bug that only a
packaged build made obvious, and they constrain the API.

**`UFolioWidget` builds its canvas with no document.** `RebuildWidget` creates the canvas, applies every
recorded binding, and *then* calls `LoadDocument`. Passing `DocumentPath` to the canvas's `Construct`
loads it immediately, which is before any `BindViewModel` call can have run - so every binding rendered
as literal braces and the cause was nowhere near the symptom.

`BindViewModel` is therefore callable at any time. It records the binding (so a rebuild - designer
preview, re-add to the viewport, hot reload - puts the same models back), applies it if the canvas
exists, and rebuilds the document if one was already up. That last case logs, because it is not free.

**Mock data is compiled out of Shipping and yields to game code.** `SFolioCanvas::MountDataModel` used to
mount `ui/mock/<model>.json` in every configuration. Two consequences, both live: a retail build rendered
invented values for any screen whose model the game forgot to bind, instead of failing visibly; and the
mock occupied the name, so `BindObjectModel` on it failed. Now the whole path is `#if !UE_BUILD_SHIPPING`,
gated on `folio.MockData`, and skipped outright when `HasObjectModel` says game code got there first.

`folio.MockData 0` is worth knowing: it shows a screen as a player would see it if the binding were
missing, which is otherwise only discoverable by packaging.

## Localisation: the gather cannot see markup, so a script makes it visible

`GatherTextFromSource` scans C++ for `NSLOCTEXT`. It knows nothing about `.rml`, so the strings in a
Folio document are invisible to it and a manifest gathered without help simply lacks them, silently.

`Tools/locgather/gather_rml.py` walks the documents, reads every `data-loc` key with the text beside
it, and writes **one generated header that nothing includes** - it exists to be read by the gatherer,
not compiled. `scripts/loc.sh` runs it and then `GatherText`, in that order, because running the
second without the first is the failure this whole arrangement exists to prevent. Bash rather than
PowerShell so a Linux developer can use it: engine discovery reads the registry on Windows and
`~/.config/Epic/UnrealEngine/Install.ini` on Linux, with `UE_ROOT` overriding both. `reg query` emits
CRLF and an engine path routinely contains spaces, so parsing its output by last field silently
truncates it.

**The UI is its own localization target.** `Config/Localization/Folio.ini` gathers only
`Source/Generated/`, producing `Folio.manifest`, `Folio.archive`, `Folio.po` and `Folio.locres`. So the
file handed to a translator holds the screens and nothing else, rather than every C++ string in the
project. Two consequences that are easy to get wrong:

- **The target has to be registered for the runtime to load it**, with
  `+LocalizationPaths=%GAMEDIR%Content/Localization/Folio` under `[Internationalization]` in
  `DefaultGame.ini`. Without it the manifest is correct, the archive has translations, and every string
  still renders in English with nothing logged. This is the setup mistake to check first.
- **The game's own target must exclude `Source/Generated/*`**, or the same strings land in two
  manifests and appear twice in a word count.

**Deliberate deviation from the original plan:** the brief said to add `FileNameFilters=*.rml` to the
gather step. Emitting a header instead means there is no custom filter for the Localization Dashboard
to drop, which removes the hazard rather than documenting it. `Config/Localization/Folio.ini` still says
never to use the Dashboard for gather settings, because it regenerates that file from its own store.

**The namespace rule is duplicated across two languages and both must agree.**
`Folio::Loc::NamespaceForDocument` and `namespace_for` in the script both derive `Folio.MainMenu` from
`ui/screens/main_menu.rml`, and `Folio.Common` from anything under `ui/lib/`. If they disagree, every
lookup misses and every string falls back to English with nothing logged. The script fails on two
documents deriving the same namespace, which is what stops the file-stem rule silently merging
`ui/hud/player.rml` and `ui/screens/player.rml`.

**Runtime resolution is a document pass, not `TranslateString`.** That hook receives a string with no
element context, so it cannot tell whether the owning element carries a key: it can neither look one up
nor notice text that has none, and wiring both would translate twice. The pass runs in `LoadDocument`
before the first update, so translated text is laid out rather than reflowed a frame later.

Three kinds of element are skipped, and the reasons differ:

- **Containing child elements.** Translation replaces the element's entire contents, so a child would
  be swallowed into the string and come back out as escaped, visible markup. The key belongs on an
  inner text-only element.
- **Driven by a data binding.** Its value is game state, and rewriting the RML destroys the view RmlUi
  built at load, leaving the binding permanently dead.
- **Empty.** Nothing to gather, so nothing to resolve.

`FText::AsLocalizable_Advanced` rather than `FindTextInLiveTable_Advanced`: the former returns the
source string for a key that is gathered but untranslated, which is every string in the culture it was
authored in. The latter is used only by `folio.Loc.Report`, where the question is whether a translation
exists at all rather than what to display.

**Verification, and how to repeat it.** `folio.Loc.Report [document]` lists every key with `[loc]` or
`[source]`. Run it in a `-game` session rather than PIE:

```bash
UnrealEditor.exe <project>.uproject -game -windowed -resx=640 -resy=360 `
  -ExecCmds="folio.Loc.Report ui/screens/settings.rml" -log=Loc.log
```

To prove text actually swaps, patch a translation into `Content/Localization/Folio/en/Folio.archive`
and regenerate only the locres. **`-DisableSteps` does not work** - the run above ignored it and
executed every step, and step 3 rewrote the patched translation back to the source text. Use a
throwaway config holding `[CommonSettings]` plus a single `[GatherTextStep0]` of
`GenerateTextLocalizationResource`, and note that invoking that commandlet directly with
`-run=GenerateTextLocalizationResource` crashes: it has to go through `-run=GatherText`.

The manifest and archives are **UTF-16**, so reading one with a UTF-8 decoder fails on the first byte.

## The scripts are bash, including the Windows-only ones

Every script under `scripts/` is `.sh`. That includes `build.sh` and `build_ue_libs.sh`, which are
Windows-only today because they build MSVC static libraries: the reasoning is that a `.sh` can grow a
Linux branch later and a `.ps1` never can, so when Folio builds on Linux `folio_import_msvc` gains a
sibling in `vsenv.sh` rather than a second script somewhere else.

The logic-heavy ones are python behind a thin `.sh` wrapper, following what `loc.sh` already did:
`Tools/shaderparity/check_parity.py`, `Tools/verify/verify.py`, `Tools/verify/visual_tests.py`. They
read and compare JSON, which a shell does badly, and python is already a dependency.

**Importing the MSVC environment from bash needs a generated batch file.** The environment lives in a
cmd process, so it has to be dumped with `set` and read back. Handing the command straight over as
`cmd //c "\"$vsdevcmd\" ... && set"` does not work: bash's escaping and MSYS's path mangling fight over
the quoting and cmd ends up seeing a literal `\"` as the command name. Writing a one-line `.bat` that
does its own quoting on the cmd side, then running that, is reliable. Two further traps in the same
place:

- **`MSYS_NO_PATHCONV=1` suppresses the `//c` to `/c` rewrite**, so cmd receives `//c`, ignores it, and
  opens an interactive shell that returns its banner and hangs. Use one or the other, never both.
- **Only `PATH` may be converted to POSIX form.** `cygpath -up` it so bash can resolve `cl.exe`, and
  leave `INCLUDE`, `LIB` and `LIBPATH` as Windows paths, because the compiler reads them natively.
- Names cmd exports that are not legal shell identifiers have to be skipped, or `export` is a syntax
  error rather than a warning.

**The ports were checked against the originals rather than assumed equivalent.** `check_parity.py`
reports the same 779 tokens, all 40 metrics reports from `verify.sh` are byte-identical to the
PowerShell run's, and the visual suite renders 109 of 109. One thing that had to be preserved
deliberately: PowerShell stringified a dp ratio of `1.0` as `1`, so the report and screenshot names are
`name@1` rather than `name@1.0`, and a baseline directory recorded before the port still compares.

## Two-way binding has to write through the property, not into the mirror

`data-value` on an input writes back through the data model, and the object bridge originally could not
do it *while looking as though it could*. `Constructor.Bind(name, &Field->FloatValue)` hands RmlUi the
address of the mirrored copy, so a drag set the mirror, the next `Refresh` overwrote it from the
`UPROPERTY`, and the value moved for one frame and sprang back. Silent, and worse than refusing.

The fix is a custom `Rml::VariableDefinition` bound by the address of the **field record** rather than of
the mirrored value, so `Set` can reach the source object. `FFolioObjectModel::Write` then converts by
kind: `SetPropertyValue` for a bool, `SetIntPropertyValue`/`SetFloatingPointPropertyValue` for a number,
and `ImportText_InContainer` for everything else - the exact inverse of the `ExportTextItem` the read
path uses, so an FName, an FText, an enum by name or a struct all round-trip.

Three things about it that are not obvious:

- **It reads back after writing** rather than trusting the value it wrote. A property may clamp,
  quantise or round, and the document has to show what the object actually holds.
- **It broadcasts the field change** when the source notifies. A settings screen showing one value twice
  - a slider and the number beside it - is the ordinary case, and without the broadcast only the control
  that was touched moves.
- **`BlueprintReadOnly` and `EditConst` mean refuse, with one warning per field.** A slider bound to a
  read-only value simply will not move, and the reason is otherwise invisible. Note that a range control
  writes once at load, so the warning appears as the document opens rather than when anything is dragged.

**Driving a control from code needs the change event, not just the value.** `DataControllerValue`
listens for `EventId::Change` and reads its `value` parameter, so `SetValue` alone updates the control
and announces nothing - the model never hears. `folio.MvvmTest set <id> <value>` therefore sets the
control and then dispatches `change` itself, which is what a widget does when a person moves it. A
checkbox is different again: its state is the `checked` attribute, so that is what a click toggles.

Verified through that command in a `-game` session: int 50 to 73, float 1.0 to 1.8, bool false to true,
string 'Player' to 'Kaede', and a `BlueprintReadOnly` float refused with the warning naming it.

## Idle cost: RmlUi already tells us when it next needs updating

The brief planned a patch for this on the premise that "RmlUi gives us no signal to work from". **That
was stale.** RmlUi 6.2 has `Context::GetNextUpdateDelay()`, public and documented as being for exactly
this, and upstream already requests updates for the cases conservative tracking would have got wrong:

| Requester | Case |
|---|---|
| `Element.cpp` | a running `animation` or `transition` |
| `WidgetTextInput.cpp` | the blinking caret |
| `WidgetSlider` / `WidgetScroll` | a held arrow's auto-repeat |
| `Context.cpp` | smooth scrolling |

So no patch, no re-basing cost across an RmlUi bump, and the animation case is upstream's rather than
ours. `SFolioCanvas::Tick` reads the delay after each update and skips the frame until it elapses;
everything on our side of the boundary calls `MarkDirty` with a reason.

**Read the delay after `Update()`, never before.** RmlUi resets it to infinity as `Update` begins and
fills it in as elements ask, so before the call it says nothing about the frame about to happen.

**The one thing that made this not work at all: a captured viewport sends a mouse move every frame.**
Whether the cursor moved or not. With `MarkDirty` at the top of `OnMouseMove`, a static document
reported "never, until input" from RmlUi and still updated 10,489 times in 90 seconds - the feature was
present and doing nothing. `OnMouseMove` now compares the rounded context-space position and modifiers
against the last event and returns early when they match, which took the same document to **2 updates**.
Compared in the coordinates RmlUi would receive rather than by `GetCursorDelta`, because a document
moving under a stationary cursor is a real move and the delta would miss it.

**`autofocus` is an attribute, not a property.** `ElementDocument::Show` defaults to `FocusFlag::Auto`
and scans for the `autofocus` *attribute*; the RCSS `focus` property has nothing to do with it. This
cost a while when the caret test kept reporting "never" - the field simply never had focus, so there was
no caret to blink. Correct behaviour, wrong test.

**Both directions are checked, by `scripts/check_invalidation.sh`.** A build where every document idles
forever would pass a check that only tested idling, and a frozen document is far worse than a slow one.
Measured: static 2 updates in 20 s reporting "never"; animated 1777 updates reporting `0.000s`; a focused
caret 19 updates on a 0.698 s timer. It drives a real editor, because what it measures exists only on the
Unreal side - uipreview has its own frame loop and would prove nothing.

## Recording a document instead of drawing it

`FFolioRenderInterface::BeginRecording` installs an `FFolioDrawList` where the Slate element list would
be, and every draw path falls into it instead. RmlUi does not know the difference: it is the same
`Context::Render()` over the same markup.

`FFolioDrawList` and `FFolioDraw` were extracted from the layer stack rather than written fresh, because
the layer path already recorded on the game thread and replayed with RHI. So there is **one**
representation of a recorded draw, and the two replays differ only in their transform and destination -
a layer uses an orthographic one into an offscreen target, a world document a world-view-projection one.

**Layer effects are refused while recording, deliberately and loudly.** A layer is an offscreen target
composited back into whatever is below it, and a recording has nothing below it. `PushLayer` returns 0
with a one-time warning naming `box-shadow`, `filter`, `backdrop-filter` and `mask-image`; `PopLayer`,
`CompositeLayers`, `EnableClipMask` and `RenderToClipMask` are no-ops. The geometry *inside* the effect
still reaches the recording, because every draw path falls through to it when no layer is open - so an
element with a box-shadow draws without its shadow rather than not at all. `settings.rml` triggers this,
which is a useful reminder that it is the common case rather than an edge one.

Clip masks are refused for the same reason: a stencil belongs to a layer. Clipping a world-space
document is the surface's business, not the document's.

**Gradients and shader decorators needed real work, not a gate.** Both rasterise to a texture and were
reachable only through `FSlateResourceHandle`, which a recording cannot use. `GetGradientResource` and
`GetEffectResource` became `GetGradientTexture` and `GetEffectTexture`, returning the texture record so
the Slate path takes `->Handle` and the recording takes the `UTexture`. The gradient path also has to
scale its UVs by the box size in the recording, because Slate did that through `DrawGeometry`'s UV scale
argument and there is no equivalent step here.

**The recording is only rebuilt when the document actually updated.** `FFolioDocumentHost::Update`
returns whether it did, which is what makes a settled world document cost nothing - the same reason the
screen path lets Slate keep its cached draw elements.

Verified with `folio.WorldTest ui/screens/settings.rml` then `folio.WorldTest report`: 48 draws, 1916
triangles at 1000x600, from the project's real settings screen. Note the report has to wait for the
component's first tick, or it reports zero and means nothing.

## RmlUi culls text that falls entirely outside the context

`ElementText::OnRender` (`ElementText.cpp:176-199`) tests each laid-out line against the scissor
region and, when nothing has set one, against `Rectanglei::FromSize(render_manager.GetViewport())` -
the context's dimensions. A line whose box does not intersect that rectangle is never submitted.

So a world-space document cannot draw text outside its own bounds. Partly outside is fine, which is
why corner elements hanging off an edge still render in full: the line intersects. Fully outside
draws nothing, silently, with no warning anywhere.

This is upstream behaviour, not something Folio's recording does, and it is not configurable without
setting a scissor region. If a document needs something to appear clear of its own background, the
background has to be a panel inset inside the document rather than the document itself - which is
what `dev/idle_animation.rml` does for its banner.

Backgrounds and images are not culled this way, only text. That asymmetry is what makes it confusing
to diagnose: a `div` with a colour appears outside the bounds and the text inside it does not.

## Two colour spaces for a world document, decided by its draw mode

A recorded colour is a byte triple, and what has to happen to it depends entirely on where it lands.

- **After tonemap.** The tonemapper has already applied the output device transform - the LUT in
  `PostProcessTonemap.usf` ends with `OutDeviceColor` - so scene colour at that point is gamma
  encoded. A byte colour goes through as written, `value / 255`, with no decode.
- **In scene.** Scene colour before post-processing is linear HDR with the view's pre-exposure baked
  in. The same byte colour must be decoded from sRGB *and* multiplied by `View.PreExposure`, because
  the tonemapper divides that back out later.
- **A layer, or a Surface document's target.** An sRGB render target, which encodes on write, so the
  decode is needed and the pre-exposure is not.

Getting this wrong is quiet. Everything renders, in the right place, at the right size; the colours
are just wrong in a way that reads as "a bit dark" rather than as a bug. `FFolioDrawList::SetDecodeSrgb`
is the switch, and the layer path and the world path disagree about it on purpose.

## A scene view does not expose the render-resolution view rect

`FSceneView` has `UnscaledViewRect` and `UnconstrainedViewRect`, both at display resolution.
The render-resolution rectangle lives on `FViewInfo`, which is private to the Renderer module, so a
scene view extension drawing its own geometry cannot set a viewport matching the view.

The way out is to set the viewport to the whole target and correct in the vertex shader, where the
view uniform buffer does know: `View.ViewRectMin` and `View.ViewSizeAndInvSize`. Clip space to the
view's pixels, then back out to the whole target's clip space. `FolioWorld.usf` does this whenever
`FolioSceneTarget.z` is set.

Deriving the rectangle from the display rect and a screen-percentage fraction instead would be wrong
under dynamic resolution, and wrong in split screen where the rect's origin is not zero.

## FPostProcessingInputs is an internal renderer header

`PrePostProcessPass_RenderThread` receives it, and it lives in
`Engine/Source/Runtime/Renderer/Internal/PostProcess/PostProcessInputs.h`. UBT does not put a
dependency's `Internal` folder on a dependent module's include path, so `Folio.Build.cs` adds it by
hand against `EngineDirectory`.

That is an engine-layout assumption. Check it on any engine upgrade: if the path moves, the in-scene
draw mode stops compiling, which is at least loud.

## Instanced stereo does not reach the world pass, and `vr.InstancedStereo` lies about itself

World documents work under instanced stereo untouched, and the reason is worth knowing before anyone
"fixes" it. Both direct routes hook post-processing — `SubscribeToPostProcessingPass(Tonemap)` and
`PrePostProcessPass_RenderThread` — and post-processing runs **per view**, after the instanced passes
have resolved. An eye index only matters to a pass that draws both eyes in one draw, which neither of
these is. `FolioWorld.usf` therefore needs no `EyeIndex` and reads one set of view uniforms correctly.

Separately: `vr.InstancedStereo` is `ECVF_ReadOnly`, so the console cannot set it, and querying it
reports "last set by constructor" even when instanced stereo is on. It is not the authority.
`FStereoShaderAspects` reads the ini directly per shader platform through
`FShaderPlatformCachedIniValue`, so the value that decides shader compilation comes from
`[/Script/Engine.RendererSettings] vr.InstancedStereo` and not from the runtime variable. Judging it
by the console readout wastes an afternoon.

Emulated stereo (`-emulatestereo` on a standalone launch) exercises the two-view path without a
headset, and is how the world pass was verified in both plain and instanced stereo.

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

### Where box-shadow stands

The layer path works end to end: a shadowed element pushes a layer, renders into a render
target, blurs it, composites and reads it back, and the element appears. What is wrong is the
colour. Measured on a `#1f1f28` panel: an unshadowed one samples `(31,31,40)`, correct; a
shadowed one samples `(79,79,90)`, far too light, and the shadow itself is not visible against
the background at all.

Two candidates, neither confirmed:

- **Gamma.** The layer target is `RTF_RGBA8` with `bForceLinearGamma = false`, so it is sRGB,
  and `FCanvas` encodes on write. Feeding it the byte values as linear encodes them twice;
  decoding first with `FLinearColor(FColor)` should round-trip, and did not change the
  measurement, which suggests the encode is not where the error is.
- **Double composition.** `GeometryBoxShadow` renders the background inside the layer *and*
  the element then draws the saved texture, so a surface drawn twice through an alpha blend
  lands lighter than either. Making `CompositeLayers` blend down without disturbing the stack
  (it was popping and re-pushing, which clears) did not change it either.

- **Premultiplied blending.** `FCanvas` was using `SE_BLEND_AlphaBlend`, which multiplies by
  alpha again; `SE_BLEND_AlphaComposite` is the premultiplied mode. Changed, and the number
  did not move either - which in hindsight is expected, because the first draw goes onto a
  cleared transparent target where both blends reduce to the same thing.

All three were genuine bugs and all three are fixed. None of them changed the measurement,
which is itself the useful result: `(79,79,90)` is stable across every blend and gamma change
made to the canvas path, so the error is not in how the layer is *drawn*.

The layer path does execute - verified by instrumenting it:

    PushLayer 785x272 origin 0,0 depth 0
    layer draw 58 tris          <- the element background and border
    PushLayer 785x272 origin 0,0 depth 1
    layer draw 18 tris          <- the shadow itself
    SaveLayerAsTexture -> target

So geometry reaches the render target and a real texture comes back. Reading the target's pixels back settled it, and it is not the colour round trip at all:

    layer centre pixel = 20,20,26 a=255 (srgb=1)

`20,20,26` is `#14141a`, the document body's background, fully opaque. The layer should hold
the panel's `#1f1f28` where the panel covers and nothing where it does not. So the layer is
capturing the **wrong content**, and every colour fix above was aimed at the wrong half of
the pipeline - which is exactly why none of them moved the number.

Two things to check next, in this order:

1. **What is being drawn into the layer.** `RenderGeometry` currently redirects *every* draw
   to the layer while one is open. If RmlUi renders anything outside the shadowed element
   between push and pop - the body background among it - that lands in the texture too.
2. **Where it lands.** `GeometryBoxShadow` renders with `info.element_offset_in_texture`,
   which positions the element inside the texture rather than in the document. The layer
   draw subtracts `LayerOrigin`, which is derived from the scissor region and was observed
   to be `0,0`; if RmlUi's offset is already texture-relative then subtracting anything is
   wrong, and if it is not, `0,0` is wrong.

Dumping a layer to a PNG rather than sampling one pixel would answer both at once.

**Before the layer work, an element with `box-shadow` rendered no background at all**, which
was worse than the shadow merely being absent. `GeometryBoxShadow` draws the element's background *into* a
layer to build the shadow texture, then draws the element from that texture alone. With no
real layer, `SaveLayerAsTexture` has nothing to return. Returning 0 is actively harmful,
because zero also means "no texture" on the geometry path, so the element paints as a solid
white rectangle over itself; returning a transparent 1x1 makes it vanish instead. Neither is
right, so `box-shadow` is out of `ui/lib/` and on the banned list in `UI_CONVENTIONS.md`
until layers exist.

Layers need a render target, which means an RHI-backed custom Slate element
(`ICustomSlateElement`) rather than `MakeCustomVerts`. That is not a departure from the
architecture decision: it still composites inside Slate's own batch order and still needs no
fullscreen target. It is the next piece of Phase 3.

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

## Toolchain

**The MSVC toolset must match whatever UnrealBuildTool picks, and guessing it was wrong.**
This was originally pinned to 14.38.33130 on the assumption that UE 5.8 selects it. It does
not: on this machine UBT uses **14.50.35717**, from a Visual Studio 18 BuildTools install, and
RmlUi built with 14.38 was being linked into modules built with 14.50. It linked, which is
the dangerous part, because RmlUi's interface passes `std::string` and `std::vector` across
that boundary and a layout change would corrupt rather than fail.

Both build scripts now default to `-Toolset latest`, which is also UBT's default. When they
disagree, UBT's `Toolchain Information` line is the authority; pass the version explicitly.

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

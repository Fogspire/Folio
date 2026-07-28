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

## Missing sprites

RmlUi's decorators resolve a value as a sprite name first and **silently** fall back to
treating it as a filename. Nothing is logged at the decorator layer; the only signal is
`TextureDatabase`'s `"Could not load texture"` warning when `RenderInterface::LoadTexture`
returns 0.

So `LoadTexture` never returns 0: on failure it returns a generated magenta checkerboard. That
suppresses RmlUi's own message (we own the wording), and the `source` string it was called with
is exactly the unresolved sprite name. This is what makes markup work before art exists.

`LoadTexture` runs **before layout**, so it cannot know what size the sprite will be drawn at.
The rendered size is captured instead from the compiled geometry's bounding box at draw time.

## Git LFS is not set up

`.gitattributes` marks `*.png` / `*.ttf` / `*.otf` as plain `binary`, not LFS, because
`git lfs install` has never been run on this machine and an undefined filter is worse than no
filter. Fine at Phase 1 scale (one font). Before Phase 2 lands icon atlases, run
`git lfs install` and switch those lines to
`filter=lfs diff=lfs merge=lfs -text`.

## Toolchain

`build.ps1` defaults to **MSVC toolset 14.38.33130**, matching what UE 5.8 selects, so the
static libraries built here can link into the Phase 3 UE ThirdParty module without an `std::`
ABI mismatch. Override with `-Toolset` if needed.

`RMLUI_FONT_ENGINE` is kept as a build parameter rather than hardcoded, because Phase 3 may
have to set it to `none` and supply a UE-backed font engine to avoid colliding with UE's own
copy of FreeType (`Engine/Source/ThirdParty/FreeType2`).

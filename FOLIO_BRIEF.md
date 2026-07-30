# Project brief: RmlUi-based UI system for Unreal Engine 5

This is the plan of record for Folio. It lives in the plugin repo so it stays with the code and is
versioned alongside it. Phase 2 describes work in a game's own repo rather than here; everything
else is plugin work.

**Decisions since superseded.** The brief is kept as written rather than rewritten, so read these
first where they contradict it:

- **Fonts are bundled.** "Do not bundle fonts or pick a typeface" was reversed: Folio ships variable
  faces under `fonts/` as defaults, and a game overrides by dropping faces in its own `ui/fonts/`.
  Resolution order is the game's faces, then Folio's, then the engine's Roboto.
- **RmlUi is patched, not forked, and `patches/RmlUi/` is not empty.** The MSDF plan's "no fork,
  patches stays empty" line was overtaken by the corner-antialiasing patch. We stay pinned to
  upstream and patch at fetch time; see Phase 6 on the dirty-query patch.
- **MVVM is optional for the game, not for Folio.** Folio ships the binding bridge; a project
  chooses whether to use view models. Phase 4's "the viewmodel as the seam" does not make
  `UMVVMViewModelBase` mandatory, and Folio deliberately does not link the MVVM plugin.

---

## Context

I am building a replacement for UMG as the authoring layer for my game's UI — HUD, menus, everything. The goal is a UI system where **the markup is plain text that an LLM can write and iterate on**, instead of hand-assembled UMG widget hierarchies. I currently spend an enormous amount of time building UI by hand in UMG and I want that gone.

The chosen library is **RmlUi** (MIT, https://github.com/mikke89/RmlUi) — an HTML/CSS-like UI library with its own layout engine. It emits vertices, indices and draw commands and expects the host application to supply the renderer, input, and file access.

**Environment:**
- Unreal Engine version: `5.8`
- Target platforms: `Windows + Steam Deck (via proton); console later`

Clone RmlUi from **upstream**: `https://github.com/mikke89/RmlUi.git` — pin a 6.x release tag. Do not use third-party forks; most are frozen around 3.x and lack spatial navigation, data bindings, flexbox, and the entire 6.0 rendering feature set, all of which this project depends on.


---

## Two repositories — know which one you are in

**Folio** (`Plugins\Folio\`) — the plugin. Engine integration only. Ships no game UI. Its documentation is `README.md`, `NOTES.md` and `UI_CONVENTIONS.md`, the last of which is the authoritative reference for what RCSS supports and ships with the plugin so it stays correct against the RmlUi version Folio pins.

**The game's UI content** (in the game project, outside the plugin) — `docs/UI_CONVENTIONS.md` (house style only; capability lives in the plugin's copy), `ui/tokens.rcss`, `ui/sprites.rcss`, `ui/lib/`, and every `.rml` and `.rcss` file.

Phases 1, 3, 4, 5, 6, 7, 8 and 9 are Folio work. Phase 2 produces content in the game repo, except the component library, which Phase 8 moves into Folio.

---

## Hard requirements

1. **Gamepad navigation is mandatory.** Non-negotiable. Menus and HUD must be fully operable on a controller.
2. **Directional navigation must be bindable to arbitrary keys** — arrow keys, WASD, and IJKL simultaneously, all rebindable at runtime from a data table. This is a primary motivation for the project; Slate/UMG makes it painful and I want it to be trivial here.
3. **Slate is a drawing surface only.** I will not author UI in Slate, and Slate's focus/navigation system must never run for RmlUi content.
4. **Must work in packaged builds**, not just PIE. File access, cooking and staging are in scope, not an afterthought.
5. **Fast iteration loop.** Editing a `.rml`/`.rcss` file must show results in about a second, without a UE compile or PIE launch.

---

## Architecture decisions already made — do not re-litigate these

These were reasoned through before you were involved. If you think one is wrong, say so and explain why, but do not silently deviate.

- **Rendering goes through `FSlateDrawElement::MakeCustomVerts`**, not raw RHI and not a render target. RmlUi's output (vertex array + index array + texture handle) maps near 1:1 onto `FSlateVertex`. This gives correct compositing with the rest of the UI, avoids a fullscreen texture, and keeps text quality good. The raw-RHI-into-render-target approach is explicitly rejected as more work for a worse result.
- **The entire UE-side surface is one `SWidget` acting as a dumb canvas.** It reports a desired size, forwards input into the RmlUi context, and emits geometry. No children, no Slate layout, no Slate navigation. Treat it like an HTML `<canvas>`.
- **Navigation uses RmlUi's built-in RCSS spatial navigation** (`nav`, `nav-up/right/down/left`, `:focus-visible`, `focus`, `tab-index`). Do not write a custom spatial navigation system. Read https://mikke89.github.io/RmlUiDoc/pages/rcss/user_interface.html before touching this.
- **All directional input is remapped to `Rml::Input::KI_UP/DOWN/LEFT/RIGHT`** in our input layer. Gamepad d-pad, analog stick (with deadzone and repeat throttling), and any keyboard keys all funnel through the same table. RmlUi does not care what produced the key.
- **No JavaScript.** Interactivity is expressed as: RmlUi data bindings, RCSS state classes (`:hover`, `:active`, `:focus-visible`, `:checked`), and C++ event listeners on named events. Do not add the Lua plugin unless I ask.
- **The standalone preview tool is built first**, before the UE plugin. It is the critical dependency for the whole workflow, not a nice-to-have.

---

## Explicit non-goals

- Do not build a Figma importer or any design-tool integration.
- Do not attempt `transform: rotateX()` / `perspective` support. 3D transforms do not map onto Slate's 2D render transforms. 2D transforms, opacity, colour and position only.
- Do not write a Blueprint-heavy API. A thin `UUserWidget` wrapper for placement plus a C++ data-binding surface is enough.
- Do not build a visual editor.
- Do not bundle fonts or pick a typeface. Folio defaults to Unreal's `Roboto` (`Engine/Content/Slate/Fonts/`) and exposes font registration as configuration. Typeface choice is a per-game decision.

---

## Phasing

Work through these in order. **Stop at the end of each phase and wait for me to review** before starting the next. Do not scaffold all phases up front.

### Phase 1 — Standalone preview tool (`uipreview`)

A native executable, completely independent of Unreal. RmlUi plus one of its bundled backends. Upstream documents `GLFW_GL3` first, with vcpkg handling dependencies (`vcpkg install freetype glfw3`) — use it unless you have a reason not to, and say so briefly either way.

Requirements:
- `uipreview <file.rml>` — loads and renders the document in a window, with hot reload on file change.
- `--screenshot <out.png>` — render one frame headless-ish and write a PNG, then exit.
- `--size <WxH>` — set the viewport, so I can check multiple resolutions.
- `--data <mock.json>` — mount a mock data model so HUD bindings have values to display.
- `--culture <code|pseudo>` — render with a given translation loaded, plus a `pseudo` mode that inflates string lengths ~40% and adds accents. Long-string layout overflow is the most common localization bug in game UI and this is how it gets caught before translation exists.
- `--keys "down,down,right,enter"` — inject a scripted sequence of navigation inputs.
- `--shot-each <dir>` — with `--keys`, write a PNG after each input.
- Enable RmlUi's debugger plugin (`Rml::Debugger`) behind a `--debug` flag.
- Non-zero exit code and a clear message on RCSS/RML parse errors — this needs to be machine-readable enough that a tool can tell whether a document is valid.
- **Missing-sprite handling.** Any sprite name referenced but not declared in a loaded `@spritesheet` must: render as an obvious magenta checkerboard with the sprite name drawn on it, emit a warning on stderr, and append an entry to `ASSETS_WANTED.md` recording the name, the pixel size it was rendered at, and the document that requested it. See Phase 2 for why this matters.
- **Reload granularity.** An `.rcss` change should clear the stylesheet cache and call `ReloadStyleSheet()` on affected documents, preserving the DOM — and therefore focus, scroll position and data model state. Only an `.rml` change should unload and rebuild the document. Before an RML reload, capture the focused element ID and scroll container offsets and restore them afterwards.

`--keys` combined with `--shot-each` is the important one: it is how navigation order gets verified. A static screenshot cannot show whether focus moves correctly, and that is exactly what tends to be subtly wrong.

**Gate:** I can point it at a hand-written `.rml` file and get a window, a screenshot, and a verifiable focus sequence.

### Phase 2 — Conventions and component library

*This phase produces content in the game repo, not in Folio.*

- **`docs/UI_CONVENTIONS.md` already exists.** Do not write a new one. Review and extend it against the pinned RmlUi version's property index (https://mikke89.github.io/RmlUiDoc/pages/rcss/property_index.html) and the RML element index, and correct anything that has drifted. Its supported-property list was transcribed from the documentation rather than recalled, so treat discrepancies as version differences to verify, not as errors to overwrite wholesale.
  - Since split in two: everything that would be true for any game on Folio moved to `Plugins/Folio/UI_CONVENTIONS.md`, and `docs/UI_CONVENTIONS.md` keeps house style. New capability facts go in the plugin's copy.
- `ui/lib/` — a hand-built component vocabulary: button, menu item, panel, list, slider, toggle, tab bar, modal. Each with its styling, its nav attributes, and its focus-highlight animation already correct.
- `ui/tokens.rcss` — colours, type scale, spacing. RCSS has no custom properties or `var()`, so tokens are concrete classes.
- `ui/sprites.rcss` — the single authoritative `@spritesheet` declaration of every piece of art that exists.
- Every component gets an example document under `ui/examples/` that `uipreview` can load.

**Asset discipline — already stated in the conventions doc, restated here because it is load-bearing.** Markup may reference *sprite names only*, never image file paths. RmlUi's decorators look up a sprite name first and silently fall back to treating the value as a filename, so a path reference to art that doesn't exist fails quietly. Sprite-names-only converts that into a detectable unresolved name, which is what makes the Phase 1 placeholder mechanism work.

The point of this is that screens remain fully functional — layout, navigation and animation all correct — before any art exists. Art gets produced in batches against the accumulated `ASSETS_WANTED.md`, not per screen.

Also in Phase 2:
- **Prefer procedural decorators over textures** wherever they'll do: gradients (linear/radial/conic), box-shadow, border-radius, masking, filters and backdrop filters, and font effects (glow, outline, shadow, blur). Most panel backgrounds, glows, bevels, dividers, vignettes and stylised text need no art at all. **Check `NOTES.md` first** for which of these Folio's renderer actually implements — see Phase 3.
- **Use ninepatch or tiled-box for anything resizable** — panels, buttons, frames, bars. One texture then serves every size, which is what keeps the total asset count in the tens rather than the hundreds.
- **Icon atlas.** Build the icon set from an existing open library (game-icons.net is CC-BY and game-appropriate; Lucide or Phosphor for UI chrome). Write a script: folder of source images → packed atlas → emitted `@spritesheet` block. Adding art should be dropping a file in and re-running it.
- **Author sprites at 2x** and use media queries on dp-ratio to swap resolutions, rather than redoing the kit for high-DPI later.
- **No inline `style=""` attributes.** Recommended by RmlUi for performance, but more importantly inline styles live in the DOM and won't pick up a stylesheet-only hot reload.
- **All user-visible text carries a `data-loc` key** from the first component onwards: `<p data-loc="start">Start Game</p>`. The source text stays inline so it's readable and previewable; the key makes it gatherable. See Phase 5.

Animation discipline: **animate `transform`, `opacity` and colour.** Animating `width`/`height`/`margin`/`padding` triggers reflow and is the main way to make this slower than UMG.

**Gate:** I can ask for a settings screen composed from the library and it renders correctly with working controller navigation, all without Unreal being involved.

### Phase 3 — UE plugin: rendering

- ThirdParty module wrapping RmlUi, pinned version, `Build.cs` per target platform.
- `Rml::SystemInterface` — elapsed time, logging into `UE_LOG`, cursor, clipboard.
- `Rml::FileInterface` routed through `IFileManager`. **Non-optional.** RmlUi's default implementation uses `fopen` and will fail in a packaged build.
- `Rml::RenderInterface` via `MakeCustomVerts`, including scissor regions mapped onto Slate's clipping stack.
- **Use RmlUi's compiled-geometry API** (`CompileGeometry`/`RenderCompiledGeometry`/`ReleaseCompiledGeometry`). Re-uploading geometry every frame throws away the main performance advantage over UMG. This is the easiest thing to get wrong.
- Texture handling should assume UI textures are atlased. Many small textures means many texture switches, and Slate cannot batch effectively across custom-verts elements.
- **Font registration defaults to Unreal's `Roboto`**, with additional faces registerable via plugin settings. Do not bundle a typeface. *Reversed when the distance-field text engine landed: Folio ships Inter and RobotoMono, both variable, because variable axes are most of that feature's value and the engine's Roboto is a static face. A game's own `ui/fonts/` still wins. See `NOTES.md`.*
- **Decide explicitly which advanced rendering features to implement, and record the decision in `NOTES.md`.** RmlUi 6.0's `box-shadow`, `filter`, `backdrop-filter`, `mask-image` and gradient decorators each depend on corresponding render-interface features (layers, clip masks, filters, shaders). If the renderer doesn't implement them, those RCSS properties **silently do nothing** — they parse fine and produce no output. The render interface docs include a table mapping properties to the features they require; work from it rather than guessing. Anything not implemented must be moved to the banned list in `UI_CONVENTIONS.md` on the same day, or the UI layer will be authored against capabilities that don't exist.
- **Premultiplied alpha.** RmlUi 6.0 supplies vertex colours and generated texture data premultiplied, and expects the blend mode set accordingly; our own textures should be converted to match. Getting this wrong produces dark fringing around text and translucent edges that is easy to misdiagnose as a filtering problem.
- **Implement the clip mask API.** Required for correct clipping of transformed elements — without it, anything with `transform` plus `overflow: hidden` or `border-radius` clips incorrectly.
- **Use `rmlui_visual_tests` as the correctness harness.** RmlUi ships a large visual test suite (CMake `BUILD_TESTING`). Run our renderer against it and compare with one of the built-in backends. This is a ready-made way to catch render-interface bugs that a hand-written test document would miss, and it should be working before the gate below.

**Optional, only if the art direction requires it:** additive or screen blending is not reachable from RCSS, but does *not* require forking RmlUi. Write a custom decorator that compiles a named shader with parameters via `CompileShader(name, dictionary)`; our `RenderShader()` implementation then draws that geometry with whatever blend state we choose. `Rml::DecoratorShader` in the library is the reference implementation. Decide during this phase whether we need it — retrofitting after the component library exists is more disruptive than adding it now.

**Gate:** a static `.rml` document renders in PIE, correctly composited with existing UMG, and `rmlui_visual_tests` passes against our renderer.

### Phase 4 — UE plugin: input and navigation

- The `SWidget` canvas: focus handling, input forwarding, desired size.
- Input mapper driven by a data table: gamepad d-pad, analog stick (deadzone + initial delay ~400ms, repeat ~120ms), and keyboard sets (arrows, WASD, IJKL) all mapping to `KI_*`. Runtime-rebindable.
- Confirm/back/tab-shoulder actions.
- Text input and keycode translation.
- **Input device tracking.** Detect the last-used device (gamepad / keyboard / mouse) and expose it to the data model, so documents can swap button glyphs and so the focus ring can be suppressed during mouse use. `:focus-visible` gives you most of this behaviour already — don't fight it.
- **UI audio hooks** — events on focus change, confirm, back, and invalid. Trivial now, irritating to thread through later.
- A thin `UUserWidget` wrapper so documents can be placed via existing UMG plumbing if needed.

**Data flow — MVVM, with the viewmodel as the seam.**

Three layers, each with one job:

- **ViewModel** — a `UMVVMViewModelBase` subclass, C++ or Blueprint, with `FieldNotify` on everything bindable. Owns view state and derived values. This is where "is the reload button enabled" lives, not in the view and not in the gameplay object.
- **Folio bridge** — reflects the viewmodel into an RmlUi data model, subscribes to its field notifications, and dirties the corresponding data variable on change. Written once, applies to every screen.
- **View** — `.rml` plus `.rcss`, no logic. Commands go out through `data-event-*`.

Using Unreal's own viewmodel type rather than inventing one means the existing MVVM tooling, the Blueprint authoring path and any viewmodels already written for UMG all keep working against Folio views.

**Not everything goes through the viewmodel.** The frequency split decides: values that change per frame bypass the Blueprint VM and are pushed straight into the data model from C++, because a Blueprint field-notify per frame per value is the cost that makes this slower than the UMG it replaces. Event-driven values do not need the bypass and should take the ordinary viewmodel path, where the tooling and the change tracking are worth having.

**Gate:** full controller navigation of a Phase 2 menu inside PIE, and the same menu navigable with all three keyboard schemes at once.

### Phase 5 — Shipping concerns

- Cook and staging (`+DirectoriesToAlwaysStageAsUFS` or a proper asset type) so documents, stylesheets, fonts and images land in the package.
- **Hot reload in-editor** via `IDirectoryWatcher`, with the same RCSS-vs-RML granularity and state preservation as Phase 1. Texture and atlas changes must also invalidate the `RenderInterface` texture cache — easy to miss.
- **Editor path resolution must point at the loose source files**, not a cooked or copied location. Getting this wrong makes hot reload appear broken while it's actually serving a stale copy.
- **Dev-only loose-file override in the `FileInterface`**: if a file exists in an override directory beside the executable, prefer it over the pak version. Gate behind a `-uidev` flag and `#if !UE_BUILD_SHIPPING`. This is what allows iterating on UI on the target device — including the Steam Deck — against a real build.
- Enable `Rml::Debugger` in editor and development builds.
- Data-binding bridge from gameplay code, with the model names documented in `UI_CONVENTIONS.md`.
- Optional `SRetainerWidget` wrapping for mostly-static screens.

**Localization — text must end up in Unreal's normal pipeline, not a parallel one.**

The mechanism is proven; Coherent Gameface does the same thing for HTML. `GatherTextFromSource` is a text scanner configurable by file extension, and `NSLOCTEXT` declarations placed inside XML comments are picked up regardless of position in the file.

- Add `FileNameFilters=*.rml` to the `GatherTextFromSource` step, with `SearchDirectoryPaths` pointing at the UI directory. No custom commandlet required.
- Write a pre-gather script that walks `.rml` files, reads every `data-loc` key and its inline source text, and emits a generated file of `NSLOCTEXT(namespace, key, "source text")` declarations for the gatherer to consume. Run it as a pre-step in the localization pipeline.
- **Namespace is derived mechanically from the file path by the script** — `ui/screens/main_menu.rml` → `UI.MainMenu`, anything under `ui/lib/` → `UI.Common`. Never hand-authored, never a decision anything writing markup has to make. Keys only need to be unique within their namespace.
- The script must **fail loudly** on: duplicate keys within a namespace, a `data-loc` attribute with no text content, and any text node lacking a `data-loc` attribute. That last one is the important one — it's how hardcoded untranslatable strings get caught, and it's the same principle as the missing-sprite mechanism in Phase 1.
- Implement `TranslateString()` on the system interface (or a document-load pass over `data-loc` elements) to resolve keys through `FText` at runtime.
- **The hand-written commandlet `.ini` is authoritative and lives in source control.** The Localization Dashboard and the commandlets use different config layouts and the Dashboard regenerates the commandlet version, which will silently drop the `*.rml` filter. Use the Dashboard for the Translation Editor only; never for gather settings. State this in the repo README.

Dynamic HUD strings arriving through the data model are already `FText` in C++ and are gathered by the normal source pass — no extra handling needed.

**Gate:** a packaged build runs the UI correctly on the target platform, and `-run=GatherText` produces a single manifest containing both C++ and RML text.

### Phase 6 — Performance validation

Rebuild my heaviest existing UMG menu in RML and compare against the original using `stat Slate`, `stat UI` and Unreal Insights. Report the numbers.

If it comes out worse, the cause is almost certainly one of three things, in order of likelihood: geometry being re-uploaded per frame instead of using the compiled-geometry API; animation applied to layout-triggering properties (`width`, `height`, `margin`, `padding`) rather than `transform`/`opacity`/colour; or poor batching from too many texture switches, fixed by atlasing more aggressively. Check these before concluding anything structural.

#### Idle cost: don't update a document that has not changed

There **is** one known structural disadvantage against UMG, and it is worth fixing regardless of what the comparison says. Folio currently runs `Context::Update()` and `Context::Render()` every frame for every open document, and regenerates Slate draw elements every frame, whether anything changed or not. UMG under an invalidation panel can skip all of that. A static pause menu should cost close to nothing per frame and today does not.

**RmlUi gives us no signal to work from.** There is no context-level dirty flag; `ElementDocument::IsLayoutDirty()` is private; active animations are private per-element. So this needs one of:

- **A patch exposing a dirty query — the expected route.** `patches/RmlUi/` and `folio_patch_command()` already exist and one patch is already carried, so the mechanism is proven and this costs one more patch rather than anything structural. Widen `IsLayoutDirty()` and add a context-level "needs update" that also accounts for pending style updates and running animations or transitions. Note we are pinned to *upstream* `mikke89/RmlUi` and patch at fetch time rather than maintaining a fork, so the only ongoing cost is re-basing the patch across an RmlUi bump — and `check_shader_parity.ps1` aside, nothing else notices.

  (The earlier MSDF plan's "no fork, `patches/RmlUi/` stays empty" line is stale — it was overtaken by the corner-antialiasing patch.)
- **Conservative tracking on our side** — a dirty flag set by input, data model updates, document load, size change, texture reload, and cleared after a render, plus a mechanism to keep rendering while animations or transitions run. Needs no patch, but the animation case is the hard part and getting it wrong is the failure mode below. Only worth it if the patch route turns out to be more invasive than it looks.

Then the Slate half: the canvas has to stop being painted at all when clean, which means reporting non-volatile and calling `Invalidate(EInvalidateWidgetReason::Paint)` when the dirty signal fires. Note the win depends on the host — Slate only caches draw elements inside an invalidation root, so measure with and without global invalidation rather than assuming.

**The failure mode is a frozen document**, which is far worse than a slow one, and it will show up as "the caret stopped blinking" or "the hover state sticks" rather than as anything obvious. So: opt-in behind a console variable until proven, and add a harness check that a document with a running transition still produces different frames. Things that change with no input at all, and are easy to forget: caret blink, `transition`, `animation`, `:hover` after the cursor stops moving, data model updates from gameplay, and a `@media` re-evaluation after a resize.

*Gate: a static document costs no measurable per-frame time with the variable on, and the animation harness check still passes.*

### Phase 7 — World-space documents

A document rendered onto a surface in the world: a computer terminal in a first-person or VR
game that you can walk right up to until it fills the view. That last part is the requirement
that decides the design — it must stay sharp at any distance, and `UWidgetComponent` does not,
because it renders Slate into a fixed-size render target and then samples that quad at whatever
size it lands on screen. Every mismatch is a filtered resample.

Distance fields are what make the alternative possible. Two modes, chosen per document:

- **Direct.** The document's geometry is drawn into the scene as world-space triangles with the
  field material. No render target, no resample, sharp at any distance or angle, and vector
  sprites come free because they already use that material. The cost is that layer effects -
  `box-shadow`, `filter`, `backdrop-filter`, `mask-image` - all need an offscreen target and are
  therefore unavailable, and the surface cannot composite as one translucent unit.
- **Surface.** Rendered into a render target, mapped onto a mesh. Full effects, composites as one
  surface, and the softness is avoided by driving the target's resolution from the quad's
  projected screen size each frame, quantised to steps so it does not churn. Cheap for us where
  it would not be for UMG: geometry is compiled once and glyphs are size-independent, so
  re-rendering at a new resolution costs no atlas work.

Direct is the default. Surface is for documents that actually use layer effects.

**Temporal AA must not touch a world-space document. This is the primary objective, not a nicety.**
Unreal's world-space UI looks bad mainly because TAA/TSR blurs and smears it, worst of all while
moving, and no amount of resolution fixes that. Neither mode above avoids it on its own: anything
drawn in the scene goes through the temporal resolve.

The way out is to draw after it. A `FSceneViewExtensionBase` subscribing to
`EPostProcessingPass::Tonemap` runs *after* the temporal pass, so:

- Draw the document's geometry there rather than in the base pass, and TAA never sees it.
- **Occlusion is ours to do**, by sampling scene depth and rejecting fragments behind geometry.
  Mind the resolution mismatch: with TSR or DLSS the post-tonemap target is at display resolution
  while depth is at render resolution.
- **No TAA means no temporal edge AA, so the edges are ours too** - which is exactly what the
  distance fields already provide analytically, and the corner-antialiasing patch provides for
  backgrounds. This is where that choice pays off rather than merely being nice.
- Two bonuses fall out. Colour lands post-tonemap, so authored UI colours arrive as authored
  instead of being tonemapped toward the scene's exposure. And with an upscaler the target is at
  display resolution, so the UI is native-res over an upscaled scene.
- The costs: bloom, DOF and motion blur no longer reach the document at all (mostly wanted, but it
  removes the per-document DOF choice described below for this path), translucency ordering against
  the scene becomes ours to define, and stereo needs handling per view.

This is a **third path**, not a variant of Direct or Surface: it needs its own RHI drawing rather
than going through Slate, which is a materially bigger job than the screen-space renderer was.
Budget for it accordingly.

**It is a choice, not a replacement, and the choice belongs to the game.** The two options are a
genuine trade with no right answer:

| | In-scene (Direct or Surface) | After tonemap |
|---|---|---|
| Temporal AA | Blurs and smears it | Never touches it |
| Edge quality | TAA's, plus ours | Ours alone, analytic from the fields |
| Bloom, DOF, motion blur | Yes | No |
| Colour grading and tonemapping | Applies | Does not - colours land as authored |
| Resolution under an upscaler | Render res | Display res |

A diegetic screen that should sit *in* the world - grimy monitor, lit by the scene, blurring with
DOF - wants in-scene. A waypoint marker or a readable terminal you walk up to wants after-tonemap.
So expose it **per document**, defaulting to in-scene since that is what matches an existing project's
expectations, and make it a project-wide default a game can set once.

**Per element is not practical, and it is worth saying why rather than leaving it open.** One document
produces one geometry stream that composites in order; splitting it across two render passes would
break that ordering and make layer effects meaningless, since the two halves cannot blend with each
other. A game that genuinely needs both on one surface uses two documents, which costs one more
context and is the honest answer.

*Gate for this specifically: a document on a mesh, camera moving, with no smearing or softening
under TSR - compared against `UWidgetComponent` in the same shot, which will show both.*

**Post-processing is per document, and it is a material setting, not a new pass.** A world-space
surface is translucent, so `Translucency Pass: Before DOF` (`MTP_BeforeDOF`) makes it receive
depth of field and `After DOF` exempts it. Expose it so a diegetic screen blurs with the scene
while a floating waypoint marker stays readable. Motion blur is off by default; translucency has
to opt into writing velocity, so make it a flag only if it costs little - it is not worth
distorting the design for.

Vector sprites are opt-in per document, as everywhere else.

**Gate:** a document on a mesh, legible from across a room and still sharp with the camera
against it, with DOF affecting it or not according to its setting.

### Phase 8 — Component library and authoring tools

**The component library moves into Folio.** Phase 2 puts `ui/lib/` in the game repo; that was
wrong for the same reason bundling a typeface was. A button is not game-specific, and every
project would otherwise rewrite one. What stays in the game is anything naming the game: its
tokens, its art, its bindings.

- `Plugins/Folio/ui/lib/` - structure and behaviour: button, toggle button, enum/multi-choice,
  slider, radio, checkbox, scroll box, and layout helpers. Styled **only** through token classes,
  never a literal colour, or it cannot be themed.
- `Plugins/Folio/ui/themes/default.rcss` - the tokens themselves, meant to be overridden.
- A game loads Folio's library and then its own tokens after. The cascade does the rest.

Layout note that constrains this: RCSS has `flex`, `inline-flex`, `table` and the table parts,
and **no CSS grid**. Horizontal and vertical boxes are flex directly; a uniform grid is a table
or fixed-basis flex construction, and that choice should be made once, in the library, rather
than per screen.

**Theming uses RmlUi's own mechanism**: `Context::ActivateTheme(name, activate)` with
`@media (theme: name)` in RCSS. This matters because RCSS has no `var()` and no custom
properties, so a theme cannot be a swapped variable - it is an override block against the token
layer. Which is exactly why components must not hardcode colour.

Above the components, a tier of **recipes**: complete documents a game copies and edits - main
menu, settings, pause, confirmation. Settings screens differ too much between games to
parameterise, but they are most of the way identical in skeleton, and copying a working one beats
rebuilding it.

Build both tiers opportunistically. When a screen is built for a game, anything in it that names
nothing game-specific belongs in the library instead, and should be moved there as it is written
rather than after.

### Phase 9 — Editor preview extension

`uipreview` already renders a document, watches it and screenshots it. A VSCode extension is a
webview and a spawn-on-save over the top; Rider is the same shape again, and only if it is still
wanted once VSCode exists.

**The dependency is fidelity, not the extension.** Its entire value is showing what Unreal will
show, and today the two renderers do not agree: the preview tool multisamples where Slate does
not, the colour pipelines differ, and its layer path is OpenGL rather than our RHI replay. Built
before that is closed, the tool would confidently show the wrong thing. The verification harness
comes first.

**Gate:** a document open in VSCode, re-rendering on save, matching an Unreal screenshot of the
same document to within the harness's tolerance.

#### Direct manipulation, and why it is designed for now rather than bolted on later

The preview is read-only above. It should not stay that way: selecting an element in the render,
seeing its box, and dragging it should edit the source. That is worth having for its own sake -
repositioning by eye is faster than editing numbers - and it is the only realistic route to a
designer-driven workflow if one is ever wanted, since a visual authoring tool is otherwise the one
thing a commercial UI middleware sells that Folio has no answer to.

**Barebones first, then grown.** Select, show the box, drag to reposition, edit one property. Every
later feature is an addition to that loop rather than a rework of it, *provided* the seams below
exist from the start. Retrofitting them is the expensive version.

**What has to be right early, because changing it later is disruptive:**

- **The preview must report geometry, identity and provenance, not just pixels.** `--metrics-report`
  already dumps laid-out boxes; extend it to carry, per element, the `ComputeElementPath`-style
  selector that survives a reload, and **the source file and line of the rule that set each property
  that matters**. Without provenance, a drag has no idea which rule to rewrite, and guessing produces
  inline styles nobody wants to maintain. RmlUi knows this - `Rml::Debugger`'s own element info panel
  shows which rule won - so it is a question of exposing it, possibly via one more patch.
- **Edits are a protocol, not a file rewrite.** The extension sends "set this property on this
  selector to this value"; something that understands RCSS applies it in place, preserving comments
  and formatting. Deciding this at the start is what keeps the tool from being a source mangler.
- **Selection must round-trip both ways.** Click in the render, highlight the source line; put the
  caret in a rule, highlight the elements it matches. This falls out of provenance and is most of
  what makes the tool feel like an editor rather than a viewer.
- **A rule, not an element, is usually the right target.** Dragging one button should be able to
  move every button. The tool has to make that choice visible and let the author pick, because
  silently choosing either one is wrong half the time.

**Explicitly not the goal:** a component palette, a timeline, an animation editor, or a layout
canvas that generates markup from scratch. Documents stay hand-authored text that a tool can adjust.
Anything that would make the generated markup unpleasant to read is out of scope by definition.

**Gate:** dragging an element in the extension moves it in Unreal after a save, and the diff on the
source is one property on one rule, formatted as a person would have written it.

#### Generating the view model from the document

A document already declares everything its binding surface needs: `data-model="hud"` names the model,
`{{ Health }}` names a value, `data-event-click="Reload"` names a command. Writing the matching C++ by
hand is transcription, and transcription is worth generating - for anyone, not only a designer.

**Types come from the mock file, names come from the markup.** `ui/mock/hud.json` already holds typed
values because someone wrote it to see the screen, so `"Health": 0.75` is a float and `"bAlive": true`
is a bool with no inference from expressions and no type annotations polluting the view. This also
makes the mock file do double duty and turns the two into a cross-check: a name used in markup with no
mock entry is exactly the error to report, and so is a mock entry nothing uses.

**Generate a base class, never the class itself.** `UHudViewModelBase` is generated and holds only the
`FieldNotify` properties and the `BlueprintCallable` command stubs; `UHudViewModel : UHudViewModelBase`
is hand-written and holds the logic. Regenerating the base is then always safe, and no edit a person
made is ever at risk. Generating one file that a human is expected to then own is the version that
eventually overwrites somebody's work.

Commands take their signature from the call site: `data-event-click="Damage(0.25, 'cause')"` gives
`(float, FString)`. An argument that is a bound value rather than a literal types from the mock file;
an argument that is neither is reported rather than guessed.

**The loop is a restart, not a build step**, if the project sets this in
`DefaultEditorPerProjectUserSettings.ini`:

```ini
[/Script/UnrealEd.EditorLoadingSavingSettings]
bForceCompilationAtStartup=True
```

Then "generate, restart the editor" is the whole instruction. Two things to get right: the generated
file has to be on disk before the restart, and a **failed** compile still opens the editor with the new
class simply absent - which reads as the tool having done nothing. So the generator reports what it
wrote, and the bridge already warns by name when a bound name resolves to nothing, which is what makes
that case legible.

**Gate:** a document with a `data-model` and no C++ behind it produces a compiling view model whose
values and commands both work after one editor restart, with no file a person wrote being modified.

---

## How I want you to work

- **Read before writing.** Start with RmlUi's README, the C++ manual's interface pages, and the RCSS property index. Do not rely on assumptions about CSS support carrying over.
- **Plan first.** Before Phase 1, give me a short written plan and your backend choice. Wait for approval.
- **Small commits**, one logical change each, with real messages.
- **Ask when a decision has real consequences.** Don't guess at things like asset representation or threading and then build on the guess.
- **Flag surprises immediately** — particularly if RmlUi's actual behaviour contradicts anything in this brief. This brief was written from documentation, not from having built it.
- Comment the non-obvious parts, especially anything in the render interface where RmlUi's model and Slate's model disagree.
- Keep a `NOTES.md` of gotchas discovered along the way. It will become the basis for future prompts. In particular it must record which advanced rendering features Folio implements, and the registered event names.

Begin by reading the RmlUi documentation and giving me your Phase 1 plan.

---

## Feature backlog

Wanted, not scheduled. Nothing here blocks a phase gate; pick them up between phases or after
Phase 9.

### Move dev tooling into a module that Shipping never builds

`FolioConsoleCommands.cpp` is already wrapped in `#if !UE_BUILD_SHIPPING`, so the commands are gone
from retail. `UFolioBindTest` is not, because UnrealHeaderTool generates reflection code for a `UCLASS`
regardless of any preprocessor block around it and gating one fails at link. So a retail build carries
one unreferenced class registration, which costs nothing but is untidy.

**Not a deletion.** `UFolioBindTest` is the only thing exercising the change-notification path in the
data binding bridge, so it is a permanent test fixture; the goal is to move it out of the shipping
module, not to remove it.

The supported route is a second module in `Folio.uplugin`:

```json
{ "Name": "FolioDev", "Type": "Runtime", "PlatformAllowList": ["Win64"],
  "TargetConfigurationDenyList": ["Shipping"] }
```

**There is a prerequisite, and it is the real work.** Folio's public headers currently include private
ones - `SFolioCanvas.h` includes `FolioNavigation.h` and `FolioInputState.h` - so no separate module
can compile against them today. That is worth fixing on its own merits, since it also means no game
module can include `SFolioCanvas.h`. Either move those headers to `Public/` or stop including them from
public headers.

Do this after the phases, not during: it touches the header layout everything else builds on.

### Accessibility: expose the document to assistive technology

**Today a Folio document is invisible to a screen reader.** `SFolioCanvas` is a single
`SLeafWidget`, so Slate's accessibility layer offers one opaque rectangle: none of the document's
structure, text, roles or focus state reaches NVDA, JAWS, or a console's platform narrator. Nothing
warns about it, because from Slate's point of view nothing is wrong.

This is a shipping concern rather than a nicety. Xbox certification asks about accessibility support,
and storefronts increasingly want a features declaration, so it is the kind of gap that surfaces at
submission rather than during development.

The work is additive and needs no change to how documents are authored:

- Walk the RmlUi DOM and expose it through Slate's accessibility interfaces, so the canvas presents a
  tree rather than a rectangle. The DOM already carries almost everything needed - text content,
  hierarchy, and which element has focus.
- **Roles have to come from markup**, because a `div` that behaves as a button is indistinguishable
  from one that does not. That means honouring `aria-role`-style attributes, or a Folio equivalent, and
  adding them to `UI_CONVENTIONS.md` as a requirement on interactive components rather than an option.
  Doing this while the component library is small is much cheaper than retrofitting it across finished
  screens.
- Announce focus changes. The focus events the UI sound hooks already fire are the same signal.

Not in scope here: text scaling and colour choices are design decisions the component library makes,
not plumbing this provides.

### Gradient fills for vector sprites

Today a baked vector sprite is a single-colour silhouette: the field carries coverage and the colour
comes from `image-color` or vertex colour. Art with gradients cannot be baked, which is why
`instrument.svg` could not be - radial gradients on three circles and two paths.

Feasible, and the input is already there: nanosvg parses gradients into `NSVGgradient` (type, stops,
transform) and `Tools/msdfbake/src/BakeVector.cpp` currently ignores that data entirely. Two ways to
shade it, and they compose:

- **Analytic.** Carry the gradient's type, transform and stops in the `.msdf` sidecar per sprite, and
  shade mask times gradient in the field shader. Stays fully resolution-independent. Folio already
  has gradient shading code for RCSS gradient decorators (`FolioGradient.cpp`) to draw on.
- **LUT.** Bake the ramp to a small 1D texture and sample it by gradient coordinate. Handles any
  number of stops for a fixed cost. A gradient is low-frequency, so 64 texels is plenty and the
  *shape* stays SDF-crisp - the resolution independence that matters is not lost.

Start with linear and radial analytic, since that covers nearly all UI art, and add the LUT only if
stop counts justify it.

Two things this does **not** solve, worth stating so the feature is not oversold: embedded raster
`<image>` elements in an SVG are still not vector, and per-shape gradients still need the multi-layer
sprite approach rather than one field per icon.

Related: task #26 (filled paths baking coarser than their own strokes) should be fixed first, since
gradient work would otherwise be built on top of a shape bug.

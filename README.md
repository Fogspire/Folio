# Folio

**Custom UI Rendering Framework for Unreal Engine 5.** HTML/CSS-style markup provides rapid development and iteration. Polished results in minutes with Folio instead of days with UMG/Slate. Infinitely scalable; sharp at every size. World-space UI unaffected by TAA/TSR. Surface projected UI. Lots of common game functionality out of the box.

Folio is free, source included, and built to be the UI layer of a shipping game, from a solo project to a full production, from indie to AAA.

UE5.8+

> [!WARNING]
> Fully ready to be used, however not yet fully released
> <br>Improved documentation and showcase images, as well as documentation images are pending
> <br>Changes are still reasonably frequent and not yet documented via changelog - expect to `git fetch` regularly

## Showcase

_Coming Soon_

_Released a game using Folio? Get in touch and it may appear here_

## Why Folio

### Fully Text Based

Build your entire UI in **[Folio Studio](docs/studio.md)**, a cross-platform WYSIWYG editor that draws with the same renderer the game does, without your designer needing to install Unreal.

A minute of markup results in polished animated menus. The same thing in UMG or Slate is days or weeks in the clunky Unreal Engine editor.

No binary `.uasset` - it diffs, merges, and reviews like code, because it is code. No competing for p4 locks or git conflicts.

<!-- TODO Folio Studio image -->

### Unparalleled Clarity + Limitless Scaling

Text and vector art are distance fields, resolved for the exact pixel they land on; always crisp and clear.

Regardless of resolution, or the use of `transform: scale(8)`, mid-animation, from across a room, or a VR player pressing their face into the cockpit display panel - the UI scales while remaining sharp.

Text has no atlas per font size and no blur when text grows, shrinks, or rotates. One field per glyph serves every size. Variable-weight fonts, outlines, and glows are part of the same field. Slate rasterises a bitmap for each size but Folio never does, resulting in superior fidelity under scale.

### No Smeared Post-Processing from TAA/TSR, Motion Blur

A text document on a computer screen, a sign or a cockpit panel is drawn after the tonemapper by default, where TAA, TSR, motion blur and post-processing cannot reach it.

World UI in Unreal normally smears the moment the camera moves; Folio documents do not. It is still depth-tested, so it clips geometry correctly.

Two other modes put it in the scene to take post-processing if desired.

### Surface-Projected UI

Documents can be projected onto the surface of a mesh, with input enabled, so that CRT monitor the player can actually use? Already sorted as a byproduct of a proper tech foundation: [CRT terminal tutorial](docs/tutorials/crt_terminal.md)

Surface projected documents are affected by post-processing. Any game that wants surface-projected UI with the same exceptional crispness should not use TAA/TSR - the ideal path is forward rendering with MSAA

<!-- TODO CRT screen image/video -->

### Screen-Space UI Projected to World

Folio ships with a ready-made screen-space UI component that projects into world space. This includes a ton of parameters to reproduce common game functionality, such as keeping your nameplates clamped to the edge of a screen within a certain threshold.

<!-- TODO image of nameplate and maybe params -->

### Draws Real 3D Transforms

Modern UI often have HUDs or menus with rotation. Folio supports this when using `perspective` and `rotateY`. For example:

```html
.vitals, .comms { transform: perspective(900dp) rotateY(38deg); transform-origin: 0% 50%; }
```

### Animation Built-In

Transitions, keyframes, springs that respond to the camera, and a shader decorator for scan lines, sweeps and additive glow.

Merely a single line of text for each element.

<img width="572" height="313" alt="electron_2026-10-15_09-54-23" src="https://github.com/user-attachments/assets/1a730508-cbdb-4a50-b6aa-4a2e3fa4ed5d" />

```html
.header
{
	display: flex;
	flex-direction: row;
	align-items: baseline;
	gap: 22dp;
	animation: 0.4s cubic-out cut-in;
}
```

### Common Game Functionality Out-of-the-Box

_All waiting for you in Folio Studio, either as a recipe or a tutorial. Don't waste time reproducing functionality common to many games, like UMG/Slate expects._

Nameplates. Subtitles. Dialogue/Typewriter. Paper doll/inventory. Motes TODO

<!-- TODO images -->

### Input and Menu Navigation

Keyboard and gamepad navigation works out-of-the-box, including rebinding. Want to support WASD, IJKL for left-handed users, mouse, and gamepad? Unless you are the developer responsible for CommonUI, of course you do! And if you have added Folio to your project, you already have it.

Folio provides spatial navigation, focus, modals, input prompts that swap when the player picks up a gamepad, with no focus code to write.

### Game-Driven Bindings

Bind any `UObject` by reflection: `{{ Health }}` in text, lists from arrays, commands back to C++ or Blueprint. No JavaScript and no binding code required.

### Virtual Reality Support

World documents render correctly under instanced stereo, verified with Unreal's emulated stereo. It has not yet been run on a headset. Full VR support is intended, so please raise an [issue](https://github.com/Fogspire/Folio/issues) if anything doesn't work as expected. If it does work as expected, please reach out anyway so this can be confirmed.

### AI Enabled Prototyping

A short prompt can come back as a finished working screen. Hand it [AGENTS.md](AGENTS.md). May benefit from also handing it a concept image if available.

<!-- TODO example prompt+image -->

<!-- TODO remove old notes
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
- **Quick to prototype with an AI agent, if you use one.** A screen is plain text, and the checker and screenshot tool tell an agent what it got wrong without a person in the loop, so a short prompt can come back as a finished, working screen. Hand it [AGENTS.md](AGENTS.md). May benefit from also handing it a concept image if available. -->

## Production Features

- **Accessibility.** Built with at minimum the following in mind: Screen reader that reads menus aloud, text scale, reduced motion, high-contrast settings. Build checks via verification harness. See [accessibility](docs/accessibility.md).
- **Real text shaping.** HarfBuzz shapes every string: ligatures, contextual forms, combining marks, GPOS kerning and right-to-left scripts.
- **Localisation.** Strings are keyed in markup and gathered into Unreal's own localisation pipeline, with pseudo-localisation to find layouts that only fit English before translation exists.
- **A build gate.** `--strict` fails the build on a stylesheet mistake, the accessibility checks run beside it, and a verification harness diffs text metrics and screenshots between changes. The engine side carries its own automation tests.
- **Two renderers, one result.** The standalone preview and the engine share the font engine, the distance fields and the shader maths, and a parity check fails the build if the two drift.
- **Performance.** A document that has not changed costs nothing per frame. `stat Folio` and Unreal Insights scopes cover the whole path, and expensive effects bake to textures.
- **Fits your project design.** Hosts inside an existing UMG or CommonUI tree, so CommonUI activatable widgets keep working as the screen stack and input handler around a document, binds MVVM view models or plain objects, reads Enhanced Input, and stages documents for packaging. Documents are loose files, so UI can be changed on a device without cooking again.
- **Honest limits.** [What Folio does not do yet](docs/known_gaps.md) lists every known gap in one place.
- **Source and licence.** All source is here. Use it in commercial games and change it freely; see [License](#license).

## In the box

### Menus

Main menu, pause menu, confirmation dialog, modal focus trapping, a controls screen that rebinds against Enhanced Input.

### HUD

Nameplates, world markers, bound input display.

Damage and low-health screen effects

<img width="1286" height="760" alt="mpc-hc64_2026-10-15_10-02-24" src="https://github.com/user-attachments/assets/351987e9-c955-482b-938e-00ad7a9f6dd3" />

A helmet HUD that lags and settles with the camera.

https://github.com/user-attachments/assets/6a28104d-f75e-4d6d-82de-1aaeb25269ce

### Dialogue

A fading typewriter effect. Rich text with emphasis inside one translatable line. Shared names for places via localized lookup tables.

<!-- TODO gif -->

### Inventory

A grid with drag and drop, 3D items in cells, a paper doll, a list of ten thousand items that allocates nothing while scrolling.

### Terminals

CRT and LCD terminals that you can interact with. Draggable, resizable windows, text fields, and even a recycle bin.

<img width="490" height="258" alt="electron_2026-10-15_09-49-05" src="https://github.com/user-attachments/assets/f1d531cd-b46c-4e55-b437-45c3d3ed54bf" />

### Classic Subtitles

Stacked closed captions in the classic shooter style. Several speakers at once, each line on its own timer.

<img width="343" height="161" alt="mpc-hc64_2026-10-15_10-00-29" src="https://github.com/user-attachments/assets/e57a2ac5-a5ba-4da9-a59a-bf414dd95e9c" />

### Components

Buttons, sliders, toggles, tab bars, lists, panels, scroll bars, drifting motes. All readily accessible from [Folio Studio](docs/studio.md)'s library.

<img width="363" height="326" alt="electron_2026-10-15_09-47-53" src="https://github.com/user-attachments/assets/ef980ac9-1261-4860-a97f-0353f136c038" />

### Authoring

Folio Studio for building your UI.

<img width="1584" height="935" alt="image" src="https://github.com/user-attachments/assets/c4e83c80-b060-46f5-be5e-6ad856ce07c7" />

`uipreview` for checks and screenshots in a build, vector and font baking, view model code generation, VSCode highlighting.

<!--
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
-->

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

To build Folio's tools or work on Folio itself, see [Building Folio](docs/building.md).

## Backend and dependency choices

**Backend: GLFW + OpenGL 3.3**, which is what upstream documents first. `RmlUi_Renderer_GL3` is the only shipped renderer implementing the full 6.x feature set (render layers, filters, clip masks, shaders); GL2 and the SDL renderer silently drop box-shadow, filters and gradients. GLFW additionally allows a hidden window, which is how screenshots are captured without a visible one.

**Dependencies come from CMake `FetchContent` rather than vcpkg**, which upstream suggests. vcpkg would need bootstrapping and roughly a gigabyte of tooling before a first build, its RmlUi port tracks whatever its baseline pins rather than the exact release we want, and its triplets fix the CRT and toolset, which fights the UE build where RmlUi has to use the same toolset as Unreal. FetchContent builds from source with whatever toolset we set, pinned to a commit hash in `cmake/Pins.cmake`, with nothing installed globally.

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

Skia's path operations are vendored in `Source/ThirdParty/SkiaSimplify/` rather than fetched. msdfgen needs them to union a glyph's overlapping contours before generating a field: without that, a stem and a bowl that intersect produce an inconsistent field, and the median reconstructs a false edge through the middle of a solid stroke.

**RmlUi is pinned to 6.2 and that pin is deliberate.** The docs site at <https://mikke89.github.io/RmlUiDoc/> is built from RmlUi's `master` branch and carries no version banner, so it documents features that are not in any release. `docs/internals.md` records the specific ones that bite.

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

## Changelog

### 1.0.0
* Initial release to the general public
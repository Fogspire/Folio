# Folio documentation

RmlUi-based UI for Unreal: markup and stylesheets instead of widget graphs, distance-field text and vector art sharp at any size, and a preview tool that lays out identically to the engine.

Read **Start here** in order if you are new. Everything after it stands alone.

## The one thing to know first

RmlUi is **not a browser** and RCSS is **not CSS**. Valid-looking CSS parses and then does nothing: `display: grid`, `var()`, `background-image`, `text-shadow`, `calc()`, `120ms`, `rgba(0,0,0,0.5)`. None of those warn.

Two habits cover it:

- Run with `--strict`. Nearly every RCSS mistake is a warning rather than an error, and a check that only looks at errors passes a broken stylesheet.
- When something does nothing, look it up in **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** before debugging it; it is usually on the banned list.

## Start here

| | |
|---|---|
| **[Hello world](hello_world.md)** | Install the plugin, get a document on screen, cook and ship it. |
| **[Folio Studio](studio.md)** | A visual editor on Windows, macOS and Linux: select, drag, resize and restyle on the real renderer, with every edit written back as text. |
| **[Folio Studio reference](studio_reference.md)** | Every panel, mouse action, shortcut and setting. |
| **[Setting up VSCode](vscode.md)** | Highlighting for `.rml` and `.rcss`, if you write markup in VSCode. |
| **[Testing a document](testing_a_document.md)** | `uipreview`, `--strict`, screenshots, and the checks that stop a broken stylesheet shipping. |
| **[Assigning a document](assigning_documents.md)** | The editor picker, portable paths, and how a wrong one is reported. |

## Building a screen

| | |
|---|---|
| **[Layout and panels](layout.md)** | Flexbox, the box model, units, and the three layout traps. |
| **[Components, themes and recipes](components_and_themes.md)** | The shipped library, why a component carries no colour, and how a game overrides it. |
| **[Text and fonts](text_and_fonts.md)** | Registering faces, variable weights, font effects and their size cap, and the small-text trade-off. |
| **[Rich text, terms and the typewriter](rich_text.md)** | `<text>`: emphasis inside a translatable line, names shared across the game, and dialogue that types itself out. |
| **[Images and sprites](images.md)** | Spritesheets, the image decorator's fit modes, ninepatch, and when a PNG is the right answer. |
| **[Vector art](vectors.md)** | SVG baked to distance fields: infinitely scalable icons, the colour-layer stack, and what a field cannot represent. |
| **[Line graphs](line_graph.md)** | `<line-graph>`: a smooth, per-point coloured line for live readings, with a dot on the newest and an average line. |
| **[Flipbooks](flipbook.md)** | `<flipbook>`: an image that plays its frames, on its own or paused, seeked and driven from game data. |
| **[Animation](animation.md)** | Transitions, keyframes, and what is cheap to animate. |
| **[The shader decorator](shader_decorator.md)** | Additive blending, scanlines and sweeps: the effects RCSS has no syntax for. |
| **[Drifting motes](motes.md)** | Atmosphere behind a menu, in six quads and no particle system. |
| **[Scrolling and scrollbars](scrolling.md)** | One serious trap, and why an incidental `overflow` confines focus. |

## Making it respond

| | |
|---|---|
| **[Input and navigation](input_and_navigation.md)** | Keyboard, gamepad and mouse; the one `nav` rule that everything else follows from; rebinding. |
| **[Form controls](form_controls.md)** | The real element types, and the tabset trap that looks like a layout bug. |
| **[Modals and focus trapping](modals.md)** | Opening a modal and keeping focus inside it. |
| **[Drag, drop and resize](drag_and_resize.md)** | Moving, dropping and resizing with no code, and where the game takes over. |
| **[UI sounds](ui_sounds.md)** | Four moments reported, nothing played. |
| **[Data binding](data_binding.md)** | Driving a document from gameplay, and whether you want MVVM. |
| **[Generating a view model](view_models.md)** | The codegen path, when reflection is not enough. |
| **[Calling into game code](hosting.md)** | Hosting a document in an existing widget tree, events, and the CommonUI focus trap. |

## In the world

| | |
|---|---|
| **[Documents in the world](world_documents.md)** | A monitor on a desk, a sign on a wall: putting a document on a surface and clicking it. |
| **[World document settings](world_document_settings.md)** | Every draw mode, property, call and console command of the world component. |
| **[Input prompts](input_prompts.md)** | Showing the key a player bound, drawn rather than authored, and swapping when they pick up a pad. |
| **[Nameplates and markers](markers.md)** | Screen-space documents that follow a world location. |
| **[Subtitles and closed captions](subtitles.md)** | Stacked captions: several speakers at once, each line on its own clock, a name and colour per speaker, sounds set apart from speech. Why the sound does not decide how long one holds, and why a line has to be held in the list to animate away. |
| **[A HUD that responds to the camera](hud_motion.md)** | Springs, so a visor lags a turn and settles. What transitions cannot express. |

## Shipping

| | |
|---|---|
| **[Accessibility checks](accessibility.md)** | Contrast, text size, hit targets, focus visibility and control names, checked in the build. |
| **[Screen reader](screen_reader.md)** | The tree NVDA, JAWS or a console narrator reads, the game's own voice, the review cursor and the `folio-*` attributes. |
| **[Presentation settings](presentation_settings.md)** | Text scale, reduced motion, high contrast, screen effects, text speed and colour-vision simulation. |
| **[Localisation](localization.md)** | `data-loc`, the gather, and previewing pseudo-loc before translation exists. |
| **[Performance and profiling](performance_profiling.md)** | `stat Folio`, the invalidation path, and what makes a screen slow. |
| **[Profiling reference](profiling_reference.md)** | Every `stat Folio` row, Insights, CSV and LLM, the invalidation trace and `uipreview` PERF findings. |
| **[Supersampling](supersampling.md)** | More samples per pixel for a slanted edge: turning it on, what 2x and 4x cost, and why text stays sharp. |
| **[Console commands](console.md)** | Everything Folio adds to the console, and what each is for. |
| **[Packaging](hello_world.md)** §7 | Documents are files, not assets: they need staging, or every screen is empty in a build that worked. |

## Tutorials

| | |
|---|---|
| **[A dialogue system](tutorials/dialogue.md)** | A speaker, a line that types itself out, and replies: markup a writer types, names the whole game shares, and where each half enters the document. |
| **[A CRT terminal in the world](tutorials/crt_terminal.md)** | The whole vocabulary in one build: an oval screen, a text field, a draggable window with a bin and an animated close, onto a curved surface with scan lines. |
| **[An inventory screen](tutorials/inventory.md)** | A grid, drag and drop between slots, gamepad navigation, and a data model behind it. |
| **[A grid inventory](tutorials/grid_inventory.md)** | Items that span several cells: grid lines from gradients, a ghost that shows whether a drop fits, locked cells, and one set of commands for mouse, keyboard and pad. |
| **[A list of ten thousand items](tutorials/long_lists.md)** | Binding a fixed-size window instead of the array, so scrolling allocates nothing. |
| **[A controls screen](tutorials/keybinding.md)** | Rebinding against Enhanced Input's own profile: a press-any-key panel that catches keys the game has bound, and what to do when one is already taken. |
| **[A helmet HUD that responds to the player](tutorials/visor_hud.md)** | A first-person visor with no art at all: a helmet made of one border, curvature without 3D transforms, two components trailing the camera and the stride, and baking the aperture glow so it stops costing a render target a frame. |
| **[A paper doll, and 3D items in a grid](tutorials/paper_doll.md)** | A character in a profile frame and meshes in inventory cells: isolating a capture without a staging area, the premultiply trap, and one rig feeding a page of targets. |
| **[The coloured screen edge](tutorials/screen_edge.md)** | Standing in fire, taking a hit, running on low health: an inset box-shadow is the whole effect, states cross-fade while events animate, then bake the lot to textures, because a full-screen layer costs a render target every frame. |
| **[A stamina wheel](tutorials/stamina_wheel.md)** | Stamina represented on a wheel, with partial, extra and temporary wheels. |
| **[A sprite that copies the player](tutorials/sprite_hud.md)** | A pixel-art character on an angled HUD panel that walks, runs, jumps, ducks and skids with the player: packing frames, a bound `<flipbook>`, and picking the clip from character movement. |

## Reference

| | |
|---|---|
| **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** | **Authoritative for capability.** What RCSS supports, what is banned, and what silently does nothing. Read it before authoring anything real: RmlUi is close enough to CSS to mislead. |
| **[AGENTS.md](../AGENTS.md)** | The cold-start brief to hand an agent building UI in a project it has not seen. |
| **[What Folio does not do yet](known_gaps.md)** | Every known limit, in one place. Read it before designing around something. |
| **[Internals](internals.md)** | Build and engine-integration hazards. Not for authoring. |
| **[Building Folio](building.md)** | Building the tools, the verification harness, and where everything is in the repository. For changing Folio itself. |

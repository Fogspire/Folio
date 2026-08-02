# Folio documentation

RmlUi-based UI for Unreal. Markup and stylesheets instead of widget graphs, distance-field text and
vector art that stay sharp at any size, and a preview tool that lays out identically to the engine.

Read in order if you are new. Everything after **Getting started** stands alone.

## Start here

| | |
|---|---|
| **[Hello world](hello_world.md)** | Install the plugin, get a document on screen, cook and ship it. |
| **[Setting up VSCode](vscode.md)** | Highlighting, a live preview beside the document, click-to-find-the-rule. Five minutes, and it is where the authoring time goes. |
| **[Testing a document](testing_a_document.md)** | `uipreview`, `--strict`, screenshots, and the checks that stop a broken stylesheet shipping. |
| **[Assigning a document](assigning_documents.md)** | The editor picker, portable paths, and why a wrong one now says so. |

## Building a screen

| | |
|---|---|
| **[Layout and panels](layout.md)** | Flexbox, the box model, units, and the three layout traps that cost everyone a day. |
| **[Components, themes and recipes](components_and_themes.md)** | The shipped library, why a component carries no colour, and how a game overrides it. |
| **[Text and fonts](text_and_fonts.md)** | Registering faces, variable weights, font effects and their size cap, and the small-text trade-off. |
| **[Rich text, terms and the typewriter](rich_text.md)** | `<text>`: emphasis inside a translatable line, names shared across the game, and dialogue that types itself out. |
| **[Images and sprites](images.md)** | Spritesheets, the image decorator's fit modes, ninepatch, and when a PNG is the right answer. |
| **[Vector art](vectors.md)** | SVG baked to distance fields: infinitely scalable icons, the colour-layer stack, and what a field cannot represent. |
| **[Animation](animation.md)** | Transitions, keyframes, what is cheap to animate and what will cost you frames. |
| **[The shader decorator](shader_decorator.md)** | Additive blending, scanlines and sweeps - the effects RCSS has no syntax for. |
| **[Scrolling and scrollbars](scrolling.md)** | One serious trap, and why an incidental `overflow` confines focus. |

## Making it respond

| | |
|---|---|
| **[Input and navigation](input_and_navigation.md)** | Keyboard, gamepad and mouse; the one `nav` rule that everything else follows from; rebinding. |
| **[Form controls](form_controls.md)** | The real element types, and the tabset trap that looks like a layout bug. |
| **[Modals and focus trapping](modals.md)** | The one interaction everybody gets wrong first time. |
| **[Drag, drop and resize](drag_and_resize.md)** | Moving, dropping and resizing with no code, and where the game takes over. |
| **[UI sounds](ui_sounds.md)** | Four moments reported, nothing played. |
| **[Data binding](data_binding.md)** | Driving a document from gameplay, and whether you want MVVM. |
| **[Generating a view model](view_models.md)** | The codegen path, when reflection is not enough. |
| **[Calling into game code](hosting.md)** | Hosting a document in an existing widget tree, events, and the CommonUI focus trap. |

## In the world

| | |
|---|---|
| **[Documents in the world](world_documents.md)** | A monitor on a desk, a sign on a wall. The three draw modes and the surface setup. |
| **[Input prompts](input_prompts.md)** | Showing the key a player actually bound, drawn rather than authored, and swapping when they pick up a pad. |
| **[Nameplates and markers](markers.md)** | Screen-space documents that follow a world location. |
| **[A HUD that responds to the camera](hud_motion.md)** | Springs, so a visor lags a turn and settles. What transitions cannot express. |

## Shipping

| | |
|---|---|
| **[Accessibility](accessibility.md)** | Contrast, hit targets, focus visibility and control names, checked in the build. |
| **[Localisation](localization.md)** | `data-loc`, the gather, and previewing pseudo-loc before translation exists. |
| **[Performance and profiling](performance_profiling.md)** | `stat Folio`, the invalidation path, and what actually makes a screen slow. |
| **[Console commands](console.md)** | Everything Folio adds to the console, and what each is for. |
| **[Packaging](hello_world.md)** §7 | Documents are files, not assets - they need staging, or every screen is empty in a build that worked. |

## Tutorials

| | |
|---|---|
| **[A CRT terminal in the world](tutorials/crt_terminal.md)** | The whole vocabulary in one build: an oval screen, a text field, a draggable window with a bin and an animated close, onto a curved surface with scan lines. |
| **[An inventory screen](tutorials/inventory.md)** | A grid, drag and drop between slots, gamepad navigation, and a data model behind it. |
| **[A list of ten thousand items](tutorials/long_lists.md)** | Binding a fixed-size window instead of the array, so scrolling allocates nothing at all. |
| **[A controls screen](tutorials/keybinding.md)** | Rebinding against Enhanced Input's own profile: a press-any-key panel that catches keys the game has bound, and what to do when one is already taken. |
| **[A helmet HUD that responds to the player](tutorials/visor_hud.md)** | A first-person visor with no art at all: a helmet made of one border, curvature without 3D transforms, and two components trailing the camera and the stride. |
| **[A paper doll, and 3D items in a grid](tutorials/paper_doll.md)** | A character in a profile frame and meshes in inventory cells: isolating a capture without a staging area, the premultiply trap, and one rig feeding a page of targets. |
| **[The coloured screen edge](tutorials/screen_edge.md)** | Standing in fire, taking a hit, running on low health: an inset box-shadow is the whole effect, and states cross-fade while events animate. |

## Reference

| | |
|---|---|
| **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** | **Authoritative for capability.** What RCSS supports, what is banned, and what silently does nothing. Read it before authoring anything real - RmlUi is close enough to CSS to mislead. |
| **[AGENTS.md](../AGENTS.md)** | The cold-start brief. What to hand an agent asked to build UI in a project it has not seen. |
| **[What Folio does not do yet](known_gaps.md)** | Every known limit, in one place. Read it before designing around something. |
| **[Internals](internals.md)** | Build and engine-integration hazards. Not for authoring. |

## The one thing to know first

RmlUi is **not a browser** and RCSS is **not CSS**. It is close enough that valid-looking CSS parses
and then does nothing at all - `display: grid`, `var()`, `background-image`, `text-shadow`,
`calc()`, `120ms`, `rgba(0,0,0,0.5)`. None of those warn.

Two habits make that a non-issue:

- Run with `--strict`. Nearly every RCSS mistake is a warning rather than an error, and a check that
  only looks at errors will pass a thoroughly broken stylesheet.
- When something does nothing, look it up in **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** before
  debugging it. It is usually on the banned list.

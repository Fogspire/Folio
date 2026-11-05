# Changelog

## 1.13.0
* `data-alias-<name>` takes a quoted constant as well as a variable, so a template placed several times can take a different label per instance. RmlUi patch `0011-data-alias-literal.patch`; run `scripts/build_ue_libs.sh` for each platform you ship. See [docs/components_and_themes.md](docs/components_and_themes.md#one-template-many-instances)
* **Size To Content** and **Max Content Width** on the Folio Document widget and the Folio Marker Document component: the document reports the size of its content, as UMG text does. See [docs/hosting.md](docs/hosting.md#sizing-to-the-content) and [docs/markers.md](docs/markers.md#fitting-the-content)
* `GetElementRect` on the widget and `GetScreenRect` on a marker give boxes in viewport pixels, for overlap tests. See [docs/hosting.md](docs/hosting.md#where-an-element-is-on-screen) and [docs/markers.md](docs/markers.md#keeping-markers-apart)
* `SetDesiredSize` changes a built widget's desired size

## 1.12.0
* **Marker Layer Z Order** in Project Settings > Plugins > Folio: where the marker layer sits among the viewport's widgets, so a HUD can draw under markers. See [docs/markers.md](docs/markers.md#if-it-looks-wrong)

## 1.11.1
* Dedicated servers build Folio without FreeType, as Slate does, so a Linux server target no longer fails at `FreeType2.Build.cs`. RmlUi is not started on a server; game code using Folio links unchanged
* **Set CommonUI Settings** (formerly Set CommonUI Console Variables) also writes `CommonButtonAcceptKeyHandling=TriggerClick` to `DefaultGame.ini`, so CommonButtons take the pad's confirm with the virtual accept click off. See [docs/input_and_navigation.md](docs/input_and_navigation.md#if-the-gamepad-confirms-the-wrong-control)

## 1.11.0
* `filter: shader("name args")` and `backdrop-filter: shader(...)` run a GPU pass the game registers with `Folio::RegisterShaderFilter`; the preview and Studio run `UI/filters/<name>.frag` in its place. See [docs/shader_filters.md](docs/shader_filters.md)
* `filter: glow(<colour> <radius> <strength>)`: a halo of any size, where a font effect stops at 0.1em
* `filter: drop-shadow` draws in the engine
* `display: grid`, `calc()`, and custom properties with `var()`, through RmlUi patch `0010-calc-var-grid.patch`. Run `scripts/build_ue_libs.sh` for each platform you ship. See [docs/layout.md](docs/layout.md#grid)
* `align-items` and `align-self` accept `start` and `end`
* `folio.Layers.HalfFloat`: layers carrying a glow, drop shadow or shader filter are 16-bit float, so dark soft gradients stop banding
* `folio.Layers.PoolBudgetMB`: idle layer targets past the budget are freed, longest idle first
* Blur scratch targets keep the layer's format, and a blur wider than the tap cap spreads its taps instead of ending in an edge
* `IFolioShaderFilter` is exported, so a filter written in a game module links

## 1.10.0
* `<meta name="folio-interactive" content="false"/>` in a document's head turns its input off whatever the host sets, so a HUD's markup keeps it from taking focus; see [docs/hosting.md](docs/hosting.md#a-hud-is-not-a-screen)
* `folio.Input.ForceNonInteractive`: turns input off on every screen document, to rule Folio out when input is lost after alt-tab
* `GetDocumentMeta`: reads any `<meta>` value from a document's head
* A change to `bInteractive` made while `folio.Debugger` is visible is kept when it hides

## 1.9.0
* `<path>`: SVG path data drawn as an antialiased stroke and fill, with joins, caps, dashes, arcs, nonzero and even-odd fills and a view box. `d` binds to data, and the `arc` and `wedge` transforms build it from a value. Clicks land only on the drawn shape; see [docs/path.md](docs/path.md)
* RmlUi patch `0008-property-id-cap.patch` raises the element property limit from 128 to 254. Run `scripts/build_ue_libs.sh` for each platform you ship
* `<scale>`: a gauge's ticks and numbers along a line or an arc; see [docs/scale.md](docs/scale.md)
* Verify compares the path and scale screenshots against references in `Tools/uipreview/tests/references`; `--update-references` records them again
* `examples/gauge.rml`: a speed dial built from `<scale>`, `arc`, `wedge` and a bound needle
* The front page shows data-driven vector gauges; [docs/vectors.md](docs/vectors.md) opens with which kind of art to use
* `BindViewModel` after a document has loaded replaces a mock model of the same name instead of failing with "Data model name already exists"
* [docs/hosting.md](docs/hosting.md): bind in a User Widget's On Initialized, since Construct runs after the document loads

## 1.8.0
* `<line-graph>`: `min`/`max` so values stay in their own units, fills (solid, hatch, crosshatch, columns, rows, dots) that fade and take the line's tones, a `baseline` to fill either side of, points on every value, and `smooth` and `step` curves; see [docs/line_graph.md](docs/line_graph.md)
* Weapon falloff tutorial: a line graph from a mock JSON file to game code; see [docs/tutorials/weapon_falloff.md](docs/tutorials/weapon_falloff.md)

## 1.7.0
* Studio: a **Sprites** panel packs a folder of pixel-art frames into an atlas and its `@spritesheet`, with a live preview; see [docs/studio_reference.md](docs/studio_reference.md#pack-sprites)
* `pack_sprites.py`: writes a `.spritepack.json` beside the stylesheet, and `--pack` packs one again, so Studio and the script share sheets

## 1.6.0
* Documentation site, generated from the markdown and published to GitHub Pages: https://fogspire.github.io/Folio/
* `<flipbook>`: an animated image from a grid sheet or numbered sprites, bindable to pause, seek or pick its frame; see [docs/flipbook.md](docs/flipbook.md)
* Sprite HUD tutorial: a pixel-art character on an angled HUD panel that copies the player's movement; see [docs/tutorials/sprite_hud.md](docs/tutorials/sprite_hud.md)

## 1.5.1
* `build_ue_libs.sh` and `build.sh`: opened from Explorer, PowerShell or cmd, the Git Bash window waits for Enter before closing, so the error can be read

## 1.5.0
* Studio: a **Language** picker on the canvas draws the document in a `UI/loc` translation or pseudo-localized, to find layouts that only fit English
* Studio: the active tab stays in view however many files are open, and the wheel scrolls the tab strip
* Studio: Electron 45
* Grid inventory tutorial and example, with items spanning several cells; see [docs/tutorials/grid_inventory.md](docs/tutorials/grid_inventory.md)

## 1.4.0
* `UFolioWidget::bPassThroughEmptySpace`: a click on no element goes to the game, so panels can sit over a world the player still clicks; see [docs/hosting.md](docs/hosting.md#panels-over-a-world-the-player-still-clicks)

## 1.3.2
* `build_ue_libs.sh`: builds with the MSVC toolset UnrealBuildTool picks rather than the newest installed, which failed to link with unresolved `__std_*` symbols when they differed

## 1.3.1
* `build_ue_libs.sh`: the engine FreeType path follows `--engine-dir`, so rebuilding against another engine no longer needs `--clean`

## 1.3.0
* `<line-graph>`: a line coloured per point between low, mid and high tones, with a dot on the newest point and an average line; see [docs/line_graph.md](docs/line_graph.md)
* World documents drawn after tonemap can set `bAfterPostProcessMaterials` to draw over the after-tonemap post process materials, so a fade or vignette never covers a menu
* World documents report the pad's back button through `OnBack`
* Struct members in bound arrays match regardless of case: packaged builds do not keep FName spelling, so `Rows[i].Position` could fail to resolve outside the editor

## 1.2.0
* Studio: a project reopens with the documents it last had open
* Editor: **Set Up Packaging** in the Folio toolbar menu stages the UI tree, stylesheets and fonts and cooks Folio's content

## 1.1.0
* `border-radius` takes percentages and elliptical corners: `50%` on a square is a circle, `a / b` gives the vertical radii, and `border-top-left-radius: a b` sets one corner
* `:disabled` is set from the `disabled` attribute on any element, which also takes it out of navigation and clicks
* `@import` and `@font-face`
* Font effect lengths resolve in `dp` and `em`
* 3D transforms render
* Layers are sized to what they draw, and unchanged layers are reused between frames
* World documents can fade under translucency; hidden ones are skipped in the world pass, and a torn-down one leaves no recordings behind
* Sampled texels are re-encoded for after-tonemap world documents
* Array rows bind nested structs
* Glyph shapes, and reading and setting the active input device
* Rebinding binds to each row's own slot and keeps bindings to the page's device
* Gradient bakes are shared between shaders
* Fixed a generated texture leak
* Stamina wheel recipe
* Studio: Activity panel, listing what keeps a document from idling
* Studio: library components in the Elements palette
* Studio: bake every document in a folder to PNG, and choose the canvas background
* Studio: documents with a declared size open at 1x scale
* Studio: picks up files created or deleted on disk, and remembers collapsed folders per project
* Studio: offers to rebuild out-of-date Unreal libraries and uipreview
* Studio: copy from the terminal, and its output is kept after a build
* Studio: Code completes tags, attributes, RCSS properties and values, classes and bindings, with hover docs and go-to-definition
* Studio: Code underlines CSS that RCSS ignores, and renderer and schema problems, on their lines
* Studio: Format Document (Alt+Shift+F) for `.rml` and `.rcss`, optionally on save, on Enter, on paste or when Code loses focus
* Studio: Preferences and Keyboard Shortcuts are under Edit; Ctrl+Shift+, opens Keyboard Shortcuts, and shortcuts with Shift and punctuation can be bound
* The Unreal build refuses RmlUi libraries built with other patches
* A new RmlUi patch applies to an existing build tree; CMake build folders configured at another path are discarded

## 1.0.0
* Initial release to the general public

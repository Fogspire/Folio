# Console commands

Everything Folio adds to the console. All of it is compiled out of Shipping: the commands put a document on screen, drive it or reload it, which is development tooling, and leaving it in retail would let a player open a test document over the game.

Type `folio.` in the console and Unreal will complete the rest.

## While authoring

| | |
|---|---|
| `folio.Show <document> [supersample]` | Puts a document on screen, over the game. `folio.Hide` to close. The sample count is 1 to 4, for looking at a slanted edge both ways. |
| `folio.ShowWindow <document>` | The same in a movable window, which is easier to compare against a reference. `folio.CloseWindow`. |
| `folio.HotReload` | Rebuilds the open documents from disk. An `.rcss` edit reloads stylesheets only, keeping the DOM, focus and scroll; an `.rml` edit rebuilds. |
| `folio.ReloadTextures` | Picks up a re-baked atlas. **RmlUi caches a texture by path forever**, so this is what makes `msdfbake` output appear without reopening. `folio.ReloadTextures all` releases every texture whether or not it looks changed. |
| `folio.Debugger [on\|off]` | RmlUi's element inspector on the focused document: the live DOM, computed properties, and which rule set what. **This is the tool for looking inside a document** - Slate's Widget Reflector sees one canvas, because that is genuinely all Folio draws through. |
| `folio.MockData` | Mounts the `UI/mock/<model>.json` a document names, so a screen shows data in PIE without the gameplay behind it. |
| `folio.MouseMode [on\|off]` | Shows the cursor and stops the game consuming the mouse. What you want before clicking anything in a HUD. |

## Themes and localisation

| | |
|---|---|
| `folio.Theme` | Lists which themes are on. |
| `folio.Theme <name> [0\|1]` | Turns one on or off across every open document, and remembers it for documents opened later. |
| `folio.Loc.Report [document]` | Every `data-loc` key and whether it resolves. The quickest way to find a key that was renamed on one side only. |
| `folio.Terms` | Every loaded term, the file it came from and the text it resolves to. |
| `folio.Terms.Reload` | Reads `UI/terms` again and rebuilds every open document, since a term is substituted while a document's runs are built. |
| `folio.Text.Skip` | Completes every `<text>` still revealing, as a player pressing skip would. For judging pacing without a controller in your hand. |
| `folio.Text.Speed [multiplier\|instant]` | How fast `<text>` types, 1 being as authored. `instant` flips whether it types at all. Its own setting rather than part of reduced motion. |

## Navigation

| | |
|---|---|
| `folio.Nav.Arrows` `folio.Nav.WASD` `folio.Nav.IJKL` `folio.Nav.Gamepad` | Each scheme on or off. A game that binds WASD to movement while a HUD is up wants that one off. |
| `folio.Nav.Wrap [0\|1]` | Navigating past the end of a list wraps to its other end. On by default. |
| `folio.Nav.SwapConfirmBack [0\|1]` | Swaps which face button confirms. Changes what the two buttons *do* and updates every open document in the same frame. |
| `folio.Nav.Bind <key> <action>` | Binds a key at runtime. Actions: `Up`, `Down`, `Left`, `Right`, `Confirm`, `Back`, `NextTab`, `PreviousTab`, or `None` to unbind. |
| `folio.Nav.Bindings` | What every action currently answers to. |
| `folio.Nav.ResetBindings` | Back to the built-in schemes. |
| `folio.NavTest [document\|off]` | Opens a boxed document with keyboard focus, so arrows, WASD, IJKL and the pad can be tried by hand. |

A binding wins over the built-in schemes and ignores the `folio.Nav.*` switches: a project that bound a key meant it. See **[input and navigation](input_and_navigation.md)**.

## Documents in the world

| | |
|---|---|
| `folio.WorldTest [document\|off\|report]` | Puts a document on a surface in front of the camera, with no actor to place. `report` prints what it is drawing. |
| `folio.World.ShowSurface` | Logs what the pointer is doing on a world document. This is how you tell "nothing is driving the pointer" from "the pointer is in the wrong place". |
| `folio.World.Occlusion` | Whether a world document is occluded by the scene. |
| `folio.World.SurfaceAutoResolution` | Whether a surface re-renders when its on-screen size changes. |
| `folio.World.Verify [document]` | Asserts the world path still applies `transform` and `opacity`. Records the document twice, once with a property set, and checks the geometry moved. |

See **[documents in the world](world_documents.md)**.

## Measuring

| | |
|---|---|
| `stat Folio` | What the UI costs per frame. Start here. |
| `folio.Invalidation` | Whether the skip-if-unchanged path is on. Turn it off to find out whether a bug is the invalidation or the document. |
| `folio.Invalidation.Trace` | Why a document is not idling. A screen that never settles is usually one element still transitioning something invisible. |
| `folio.Layers.Cache` | Whether a layer whose content has not changed reuses its picture from an earlier frame. On by default; turn it off to find out whether a stale effect is the cache. |
| `folio.Layers.CacheStats` | Logs how many layers were drawn and how many reused since the last call. |
| `folio.Layers.CacheTrace [frames]` | Logs every layer drawn rather than reused over the next frames, with what changed since the frame before: place, filters, mask, or which draw and which part (texture, positions, uvs, colours). A layer whose draw gets a new texture every frame is usually a gradient whose stops are bound to a value that keeps moving. |
| `folio.Input.Trace` | Every input event as it reaches a document, and what consumed it. |
| `folio.Font.Report` | Which faces are loaded, their variation tuples, and what they cost in atlas pages and glyphs. Finds a screen that quietly instantiated nine weights. |
| `folio.Font.ClearCache` | Drops the generated fields and regenerates. For proving a bake rather than a cache. |
| `folio.Screens` | What every screen document is laid out against, and - for a supersampled one - the sample count and how many glyph runs the last paint drew at the screen's own resolution. |
| `folio.Supersample.NativeText [0\|1]` | Whether a supersampled document keeps its text out of the target. On by default. Zero is the comparison: everything through the target, text included. |
| `folio.Marker.Test [distance] [document]` | Puts a marker in front of the player with a fade band, so the distance settings can be seen without building a level for them. Again to remove it. |

See **[performance and profiling](performance_profiling.md)**.

## Accessibility

| | |
|---|---|
| `folio.Accessibility.Dump [delay]` | What a screen reader is read out of every screen document, in reading order. From the live tree when one is running, and from the document itself when one is not. A delay defers it, which a menu that closes on losing focus needs. |
| `folio.Accessibility.Force 1` | Builds the tree with nothing listening. For measuring what it costs, and for dumping the real widgets rather than what would be built. Game code that needs the same thing takes a `FFolioDocumentTreeRequest` instead, which `Dump` names. It also makes the document's structure appear under the canvas in Slate's Widget Reflector, named by what each node says - take the snapshot *after* running this, and expect the picker to keep landing on the canvas, since the proxies are hit-test invisible. `folio.Debugger` is the better tool for this. |
| `folio.Accessibility.Reader [0\|1]` | Turns the game's own screen reader on or off, which is what an options menu toggle calls. No argument flips it. With `LogFolio` at Verbose, every sentence it speaks is logged. |
| `folio.Accessibility.Say <text>` | Announces a string through whatever screen reader is running. The same path `folio-announce` and `UFolioAccessibilityLibrary::Announce` take. |
| `folio.Accessibility.Review [what]` | Drives the review cursor, which reads the screen without moving focus: `list`, `next`, `previous`, `first`, `last`, `heading`, `prevheading`, `repeat`, `all`, `stop`, `activate`, `focus`. No argument lists every line it can reach, with `>` on the one it is standing on. |
| `folio.Accessibility.Verbosity [low\|normal\|high]` | How much the reader says beyond a control's name. Also reports how many lines are waiting to be spoken, which is what to watch if a screen sounds like it is talking about the past. |
| `folio.Accessibility.TextScale [scale]` | Scales every type size written in `rem`. No argument reports the current one. |
| `folio.Accessibility.ReducedMotion [0\|1]` | Turns every transition and animation off across every document. No argument flips it. |
| `folio.Accessibility.ScreenEffects [0\|1]` | Whether the layers marked `folio-screen-effect` are drawn: the vignettes, tints and flashes a game paints over the whole view. No argument flips it. |
| `folio.Accessibility.HighContrast [0\|1]` | The high-contrast theme, under the name a game's setting should call rather than a theme string. |

See **[accessibility](accessibility.md)**.

## Producing files

These are the engine's answer to `uipreview`'s flags, and exist so the two renderers can be asked the same question without a person watching.

| | |
|---|---|
| `folio.Bake <document> <selector> <out.png> [w] [h] [downscale]` | Renders one element alone, on nothing, cropped to its box, as a PNG with alpha - so an effect costing a render target every frame becomes a texture. The engine rather than `uipreview`, because only this renderer draws `box-shadow`, `filter`, `backdrop-filter` and `mask-image` the way the game will. `downscale` renders at [w]x[h] and writes a fraction of that, which is how a soft full-screen wash becomes a texture worth loading. |
| `folio.Surface.Shot <document> <out.png> [w] [h]` | Renders a document the way a surface does and writes a PNG. A viewport screenshot would carry the game behind it; this is the document alone on a transparent ground. |
| `folio.Metrics.Report <document> <out.json> [w] [h] [dp]` | The same report `uipreview --metrics-report` writes, so where text landed can be diffed between renderers. |

## Gates

| | |
|---|---|
| `folio.Gate [document] [shot\|off]` | Shows a document between two viewport markers, for a comparison shot against another implementation. |

## Where to go next

- **[Testing a document](testing_a_document.md)**: the preview-side equivalents.
- **[Performance and profiling](performance_profiling.md)**: what the numbers mean.

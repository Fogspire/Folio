# World document settings

Every draw mode, property, call and console command of the **Folio World Document** component. To place one and wire it up, see **[documents in the world](world_documents.md)**.

## Draw modes

`Draw Mode` decides which path picks the document up. New components take the default from **Project Settings > Plugins > Folio > Default World Draw Mode**.

| Mode | Drawn | Sharp at any distance | Gets bloom, DOF, grading | TAA/TSR smear | Layer effects |
| --- | --- | --- | --- | --- | --- |
| **After Tonemap** (default) | After the tonemapper, depth-tested against the scene | Yes | No | No | Skipped, with a warning |
| **In Scene** | Into the scene as world-space triangles, before post-processing | Yes | Yes | Yes | Skipped, with a warning |
| **Surface** | Into an offscreen render target a material on your mesh samples | No, softer | Bloom and grading; no DOF with `M_FolioSurface`, which draws after depth of field | Yes | Yes |

### After Tonemap

Use this unless you have a specific reason not to. Colours arrive as authored, the distance fields do the antialiasing, and under an upscaler the document is drawn at display resolution. Bloom, depth of field and motion blur never reach it.

Translucency the renderer has already composited (fire, smoke, additive glow) lands under it, so the document draws crisply over a fire in front of it. **Behind Translucency** fades it under them instead:

- Deferred renderer only. Under forward or mobile it does nothing.
- Only translucency in the **After DOF** pass (the default for translucent materials) or **After Motion Blur** fades a document. Before-DOF translucency still draws under it unless `r.Translucency.StandardSeparated` is on.
- It costs a copy of scene colour per view and six texture reads per document pixel, so leave it off for documents nothing passes in front of.
- `folio.World.Occlusion 0` turns it off along with depth testing.

### In Scene

For a diegetic screen that should sit *in* the world: a grimy monitor lit by the scene, a display that should bloom. Still no render target and no resample.

### Surface

The only mode with layer effects (`box-shadow`, `filter`, `backdrop-filter`, `mask-image`) and the only one for curved or non-planar geometry. The whole document composites as one translucent unit. It is visibly softer than the other two in play, on every document, because the document is resolved at the target's resolution and then resampled. A layer also costs about `3/255` of brightness from the 8-bit sRGB round trip, visible only side by side.

## Component properties

| Property | Default | Does |
| --- | --- | --- |
| **Document Path** | | Document to load, relative to the project directory. A leading `/folio/` resolves inside the plugin. |
| **World Size** | 100 x 60 | The surface's size in world units, before the component's scale. A wider surface shows more of the document, not the same content stretched. |
| **Pixels Per Unit** | 10 | Document pixels per world unit: the authoring scale, not a quality setting. At the default a 100x60 surface lays out as a 1000x600 document, so 16dp text reads at about screen size. Set it before assigning a document. |
| **Draw Mode** | After Tonemap | See [draw modes](#draw-modes). |
| **Always On Top** | off | After Tonemap only: skips the occlusion test, so scene geometry never hides the document. |
| **Behind Translucency** | off | After Tonemap only: fades the document under translucency in front of it. See [After Tonemap](#after-tonemap). |
| **After Post Process Materials** | off | After Tonemap only: draws after the after-tonemap post process materials, so a fade or vignette made by one never covers it. For a menu over the game. |
| **Max Surface Scale** | 2 | Surface only: the largest the target may be, as a multiple of the document's size (0.25 to 4). A memory bound, not a quality target. |
| **Update In Editor** | on | Loads, updates and draws in the level viewport. Turn it off for a document that should only exist in play, or one whose animation is distracting. |
| **Pointer From Player View** | off | Traces from the local player's view each frame and drives the pointer. |
| **Pointer Uses Mesh UV** | off | Traces the mesh and reads the UV hit instead of intersecting the component's rectangle. Costs a trace per frame. |
| **Pointer Trace Channel** | Visibility | Channel for the mesh trace. |
| **Pointer UV Channel** | 0 | UV set to read; must match the one the material samples. |
| **Invert Pointer V** / **U** | off | Flips the derived pointer axis, for a mesh whose UVs run the other way. Not applied to `Move Pointer`. |
| **Pointer Reach** | 400 cm | How far the player can be from the surface and still point at it. |
| **Show Cursor** | on | Draws a cursor on the surface at the pointer. |
| **Pointer Key** | left mouse | What counts as a click with Pointer From Player View. |

## Calls and events

| Call | Does |
| --- | --- |
| `Load Document (Path)` | Loads a different document, replacing whatever is up. |
| `Bind View Model (Name, Source, Poll Every Frame)` | Binds a UObject's properties as a named data model, as the screen widget does. |
| `Mark Dirty` | Says the document changed, for game code that reaches into it directly. |
| `Move Pointer Along (Origin, Direction, Distance)` | Puts the pointer where the line crosses the component's rectangle. Returns false and changes nothing if it misses. |
| `Move Pointer To Hit (Hit)` | Uses the mesh's own UVs from a line trace. Needs the [mesh UV setup](world_documents.md#let-the-player-point-and-click); returns false and logs why if the UV is unavailable. |
| `Move Pointer (UV)` | The primitive underneath, in 0-1 from the top left, for a UV Folio cannot work out. |
| `Clear Pointer` | Takes the pointer off, so nothing is left hovered. |
| `Press Pointer` / `Release Pointer (Button)` | A click at the pointer. Left, right and middle mouse. |
| `Scroll Pointer (Lines)` | Positive scrolls down. |
| `Focus Document` / `Release Focus` | Hands directional input to the document, and gives it back. One document at a time. |
| `Is Focused` / `Is Taking Text` | Whether the document has input, and whether a text field in it has the keyboard. |
| `Focus Element (Id, Show Ring)` | Puts focus on one element, so navigation has somewhere to start. Returns false if the id names nothing focusable. |
| `Get Element Value (Id)` | A form control's current value, as it would be submitted. |
| `Get Render Target` | Surface only: the current target, or null. |
| `Get Screen Height Pixels` | How tall the surface lands on screen now, or 0 with no view. |
| **On Surface Changed** | Fires with the target when Surface mode creates or resizes it. |
| **On Back** | The pad's back button while focused, when the document did not consume it. Escape always falls through to the game instead. |

## Surface resolution

The target is sized from how large the surface lands on screen, quantised to 128-pixel steps, so walking up to it sharpens it.

- **Max Surface Scale** bounds it: the default of 2 lets a 1000x600 document reach 2000x1200.
- It follows the surface down as well as up, and carries a mip chain filled after each render, so a distant surface does not sparkle or crawl.
- It rises the moment it is wanted and falls only after a whole step has been given up, so standing near a boundary does not flip it every frame.
- It is resized in place, so a material given it once keeps working; `On Surface Changed` still fires.
- Without a player camera, as in the level viewport, it is the document's own size.
- `folio.World.SurfaceAutoResolution 0` pins it to the document's size, as a `UWidgetComponent` does.

## The Surface material

`M_FolioSurface` ships at `/Folio/M_FolioSurface`, single sided, with one texture parameter, **`Element`**. To build your own:

| Setting | Value | Why |
| --- | --- | --- |
| Material Domain | Surface | It is a mesh in the world, not UI. |
| Blend Mode | **AlphaComposite (Premultiplied Alpha)** | Folio writes premultiplied RGB. Plain `Translucent` multiplies by opacity a second time and every edge goes dark. |
| Shading Model | Unlit | The document carries its own colour. |
| Texture sample | Sampler Type **Color** | The target is `RTF_RGBA8` and sRGB, so it decodes on sample. |
| | RGB → Emissive Color, A → Opacity | |

## Console

| Command | Does |
| --- | --- |
| `folio.WorldTest [doc] [inscene\|surface]` | Spawns a test document 2 m in front of the camera, with the player-view pointer on. |
| `folio.WorldTest report` | What it laid out at, recorded, and rendered into, against its on-screen height. |
| `folio.WorldTest off` | Removes it. |
| `folio.World 0` | Turns the world pass off entirely. |
| `folio.World.Occlusion 0` | Stops depth testing, so documents draw over everything. |
| `folio.World.Occlusion 2` | Replaces the document with the depth test's own inputs, to tell occluded from misprojected. |
| `folio.World.ShowSurface 1` | Draws the rectangle the pointer resolves against and logs misses, the pointer position and the element under it. |
| `folio.World.SurfaceAutoResolution 0` | Pins Surface targets to the document size. |

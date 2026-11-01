# Documents in the world

A terminal on a wall, a sign, a screen you can walk up to. `UFolioWorldDocument` is a scene component that loads a document and puts it on a surface in the level.

It is not a `UWidgetComponent`, which renders into a fixed-size target and resamples it, so text is soft at every distance but one. Folio's text and vector art are distance fields, drawn at the size they are seen: no intermediate target, no resample, sharp at any distance.

That is true of two of the three draw modes. **Surface mode uses an intermediate target and is softer for the same reason**, so pick it only when a document needs layer effects or non-planar geometry.

## Getting one on screen

Add a **Folio World Document** component to an actor and set:

- **Document Path**: the same path `folio.Show` takes: relative to the project directory, so `UI/screens/terminal.rml`. A leading `/folio/` resolves inside the plugin instead.
- **World Size**: the physical size of the surface in world units, before the component's scale.
- **Pixels Per Unit**: how many document pixels each world unit is worth. This is the authoring scale, not a quality setting: at the default a 100x60 surface lays out as a 1000x600 document, so 16dp text reads at about screen size. Sharpness does not depend on it.

The surface lies in the component's local XY and faces along local Z: point the component at the viewer.

To see one without placing anything, in PIE:

```
folio.WorldTest UI/screens/terminal.rml
folio.WorldTest report
folio.WorldTest off
```

That spawns the document 2 m in front of the camera, pitched up and turned off axis so projection errors show. Add `inscene` or `surface` to compare the modes.

## The three draw modes

`Draw Mode` decides which path picks the document up. The default for new components comes from **Project Settings > Plugins > Folio > Default World Draw Mode**.

### After Tonemap (the default)

Drawn after the tonemapper, where temporal antialiasing cannot touch it. TAA and TSR blur and smear world-space UI, worst while the camera moves, and no resolution fixes it. Colours arrive as authored, and under an upscaler the document is drawn at display resolution.

Bloom, depth of field and motion blur never reach it; the distance fields do the antialiasing. It is depth-tested against the scene, so it goes behind geometry correctly.

Use this unless you have a specific reason not to.

Translucency the renderer has already composited (fire, smoke, additive glow) lands under it, so the document draws crisply over a fire in front of it. Tick `Behind Translucency` to fade it under them instead:

- Deferred renderer only. Under forward or mobile it does nothing.
- Only translucency in the **After DOF** pass (the default for translucent materials) or **After Motion Blur** fades a document. Before-DOF translucency still draws under it unless `r.Translucency.StandardSeparated` is on.
- It costs a copy of scene colour per view and six texture reads per document pixel, so leave it off for documents nothing passes in front of.
- `folio.World.Occlusion 0` turns it off along with depth testing.

### In Scene

Drawn into the scene as world-space triangles, before post-processing. Still no render target and no resample, so still sharp at any distance, and it receives bloom, depth of field and colour grading like anything else in the world. Temporal antialiasing smears it.

For a diegetic screen that should sit *in* the world: a grimy monitor lit by the scene, a display that should bloom.

### Surface

Rendered into an offscreen target for a material on a mesh to sample. See below.

This is the only mode with layer effects (`box-shadow`, `filter`, `backdrop-filter` and `mask-image` need somewhere offscreen to work in), and the whole document composites as one translucent unit. It is also the only mode for curved or non-planar geometry.

The price is the resample the other two modes avoid. It is **visibly softer than the other two in play**, on every document; see [Surface mode is softer](#surface-mode-is-softer-than-the-other-two-in-play-as-well).

## Surface mode

Folio does not own the mesh. It renders the document into a `UTextureRenderTarget2D` and hands it to you.

**1. Set `Draw Mode` to `Surface`** on the component. Nothing appears until step 3. `folio.WorldTest report` tells you what size it rendered at.

**2. Make a material instance of `M_FolioSurface`.** It ships in the plugin at `/Folio/M_FolioSurface` and has one texture parameter, **`Element`**. Assign it to the mesh.

To build your own, the settings that matter are:

| Setting | Value | Why |
| --- | --- | --- |
| Material Domain | Surface | It is a mesh in the world, not UI. |
| Blend Mode | **AlphaComposite (Premultiplied Alpha)** | Folio writes premultiplied RGB. Plain `Translucent` multiplies by opacity a second time and every edge goes dark. |
| Shading Model | Unlit | The document carries its own colour. |
| Texture sample | Sampler Type **Color** | The target is `RTF_RGBA8` and sRGB, so it decodes on sample. |
| | RGB → Emissive Color, A → Opacity | |

`M_FolioSurface` is single sided. Tick Two Sided on your instance if the surface can be seen from behind.

**3. Point the material at the target.** Create a dynamic material instance on the mesh and set `Element` from the component's **On Surface Changed** event.

The target is resized in place when the resolution changes, so a material given it once keeps working. `On Surface Changed` still fires, for anything that tracks the size.

With `Update In Editor` on, the first target is created before Begin Play, so the event has already fired by the time your Blueprint binds to it. After binding, call **Get Render Target** once and apply it if it is not null. Otherwise the mesh stays blank until the first resolution change.

Everywhere else, use the event.

### Resolution

The target is sized from how large the surface lands on screen, quantised to 128-pixel steps, so walking up to the surface *sharpens* it. Re-rendering at a new size is cheap: geometry is compiled once and glyphs are size-independent.

- **Max Surface Scale** bounds it, as a multiple of the document's own size. The default of 2 lets a 1000x600 document reach 2000x1200.
- The target follows the surface *down* as well as up. A 1200-tall texture on a panel 14 pixels tall makes it sparkle rather than soften.
- The target carries a mip chain, filled after each render. Without one a distant surface is minified with a single bilinear tap, and thin strokes and small text break up and crawl.
- Resolution rises the moment it is wanted and falls only after a whole step has been given up, so standing near a boundary does not flip it every frame.
- `folio.World.SurfaceAutoResolution 0` pins the target to the document's size, as a `UWidgetComponent` does. Useful for an A/B.

Automatic sizing needs a player camera, so in the level viewport the target is the document's own size and looks softer than in play.

If the document comes up upside down, check the mesh's UVs. Document V=0 is the top.

### Surface mode is softer than the other two, in play as well

Expect this on every document. **Surface mode is the only mode that does not draw at the resolution the player sees**, and no tuning removes that entirely.

Text and vector sprites resolve their edges at whatever resolution they are drawn at. `After Tonemap` and `In Scene` evaluate those fields **at final pixels**. Surface mode evaluates them into the offscreen target, and a material then samples that texture onto a quad in perspective, so the edge is resolved once at the target's resolution and the result behaves like a bitmap.

Texel and pixel also never line up. The quad is perspective-projected and the target is quantised to 128-pixel steps, so even a target the right size on average is resampled at a slightly wrong scale across most of the surface.

**Find out which limit you are against before tuning anything.** `folio.WorldTest report` prints the target's size next to how tall the surface lands on screen, and says which is the constraint:

```
folio.WorldTest: 'UI/screens/terminal.rml' laid out at 1000x600, rendered into a 2048x1229 target,
                 covering 900 screen pixels tall
    the target is at or above the surface's on-screen size, so it is not what is limiting sharpness.
```

If the target is **smaller** than the on-screen size, raise `Max Surface Scale`.

If it is well **above** it, more resolution does not help: the surface is being minified, and the mip chain governs that.

If the two are close and it still looks soft, the mesh is scene geometry: it is rasterised at the scene's rendering resolution and resolved through whatever temporal upscaler the project runs. `After Tonemap` is drawn after both.

What helps, in order:

1. **Use `After Tonemap` unless the document needs layer effects.** `box-shadow`, `filter`, `backdrop-filter` and `mask-image` are the only reason to accept Surface mode.
2. **Raise `Max Surface Scale`.** The default of 2 is a memory bound, not a quality target. A panel the player walks up to can want more than twice the document's size on screen and is clamped to this, so it magnifies. It goes to 4.
3. **Raise `Pixels Per Unit`** so the document is authored denser. This raises the *base* the scale multiplies, and re-lays-out the document, so check the design still fits.
4. **Confirm the material is not adding its own softness**: no mip bias, and a bilinear sample.

A layer also costs about `3/255` of brightness on the way through the target, from the 8-bit sRGB round trip. Visible only side by side.

## Interacting with one

Text fields, buttons, hover, focus and the caret all behave as they do on screen. It has no widget, so nothing routes input to it by default: you say where the pointer is.

To try it, tick **Pointer From Player View**. The component traces from the local player's view each frame and drives the pointer, and **Pointer Key** (left mouse by default) is the click. `folio.WorldTest` turns this on.

That suits prototyping and a game that aims with the camera. For a cursor, a touch, or a hand in VR, leave it off and call the same path with your own ray:

| Call | Does |
| --- | --- |
| `Move Pointer Along (Origin, Direction, Distance)` | Puts the pointer where the line crosses the surface. Returns false and changes nothing if it misses, so one call answers both *is it being pointed at* and *where*. |
| `Move Pointer To Hit (Hit)` | Uses the mesh's own UVs from a line trace. See below. |
| `Move Pointer (UV)` | The primitive underneath, in 0-1 from the top left. Use it when the UV comes from somewhere Folio cannot work out. |
| `Clear Pointer` | Takes the pointer off, so nothing is left hovered. |
| `Press Pointer` / `Release Pointer` | A click at wherever the pointer is. |
| `Scroll Pointer (Lines)` | Positive scrolls down. |

`Move Pointer Along` uses the **component's own rectangle**, not the mesh, so it works whatever you modelled: place the component over the screen face with `World Size` matching it.

### Getting the cursor to line up

A cursor is drawn on the surface, on by default (**Show Cursor**). Nothing else tells you where a click lands; hover alone is too late to aim by. Turn it off for a screen nobody points at.

Watch the cursor:

- **It moves the wrong way vertically**: tick **Invert Pointer V**. Your mesh's V runs up while a document's runs down. `Invert Pointer U` does the same horizontally. This is the common one, and easy to miss because the *picture* looks right either way: a flipped V that the material corrects leaves the pointer reading the uncorrected one.
- **It drifts further off towards the edges**: the component's rectangle is not where the glass is. Check `World Size` and that the component sits centred on the screen face.
- **It does not move at all**: nothing is calling into the component. See the troubleshooting list.

`folio.World.ShowSurface 1` draws the rectangle the pointer resolves against and logs, per document, the reason a ray missed, the pointer's position in document pixels, and the element under it as `tag#id`: aim at a button, click, and the log says what RmlUi thought you hit.

### Pointing by the mesh's UVs

**Pointer Uses Mesh UV** line traces the mesh and reads the UV of the triangle hit, rather than intersecting the component's rectangle. It is exact on a curved or inset screen, where a flat rectangle only approximates the glass.

It costs a trace per frame and needs three things:

1. **Support UV From Hit Results** on, in Project Settings > Physics (needs a restart)
2. Complex collision on the mesh, `Use Complex Collision As Simple` being the easiest
3. A `Pointer Trace Channel` the mesh blocks

If any is missing the log says so.

**Pointer UV Channel** picks which UV set to read, for a mesh with more than one. It must match the channel the material samples.

The rectangle route is exact for a flat screen and needs no physics setup. Use mesh UVs when the screen is curved.

### Navigating it like a menu

Call **Focus Document** on the component and the d-pad, the arrows, WASD and IJKL all navigate it. Directional input the document acts on stops reaching the game. **Release Focus** hands input back.

Escape and the console key always fall through, and so does anything Folio has no meaning for.

Focus is one document at a time: focusing another releases the first.

A common shape is to focus on interact and release on Escape or on walking away. Nothing focuses a world document on its own, so an unused screen never takes the movement keys.

### Typing

While a text field in the document has focus, the component takes Slate's keyboard focus through an invisible widget, so characters arrive with the keyboard layout, modifiers and IME already resolved.

Keys the document does not consume fall through to the game, so movement keeps working. `Is Taking Text` says whether the document has the keyboard, and `Release Focus` blurs the field and hands it back. Give the player a way to call that, or a field left focused keeps their keyboard.

## Working in the editor

`Update In Editor` is on by default: a placed document loads, updates and draws in the level viewport. Turn it off for a document that should only exist in play, or one whose animation is distracting.

A document draws only into its own world, so a level document does not appear in blueprint or material preview viewports.

Markup and stylesheet edits hot reload in the world: no PIE restart.

## Console

| Command | Does |
| --- | --- |
| `folio.WorldTest [doc] [inscene\|surface]` | Spawns a test document in front of the camera. |
| `folio.WorldTest report` | What it laid out at, recorded, and rendered into. |
| `folio.WorldTest off` | Removes it. |
| `folio.World 0` | Turns the world pass off entirely. |
| `folio.World.Occlusion 0` | Stops depth testing, so documents draw over everything. |
| `folio.World.Occlusion 2` | Replaces the document with the depth test's own inputs, for when a document is invisible and you need to know whether it is being occluded or misprojected. |
| `folio.World.SurfaceAutoResolution 0` | Pins Surface targets to the document size. |

## If nothing appears

- **Surface mode and the mesh is blank.** Nothing is sampling the target yet, or the Blueprint bound `On Surface Changed` and never received the first one. See step 3.
- **Every edge is dark, or the whole thing is dim.** The material is `Translucent` rather than `AlphaComposite`.
- **A fire or glow in front of it draws behind it.** Tick `Behind Translucency`, and check the effect's material is in the After DOF or After Motion Blur translucency pass. A document showing faintly through dim smoke over a dark scene is expected; raise an issue with a screenshot if it reads wrong.
- **It is behind the wall it is mounted on.** Nudge it off the surface. `folio.World.Occlusion 0` confirms it.
- **Nothing hovers or clicks.** Nothing is driving the pointer. Tick `Pointer From Player View` or call `Move Pointer Along` yourself, and check `Pointer Reach` covers how far away you are standing. `folio.World.ShowSurface 1` says which of those it is.
- **The cursor moves the wrong way.** `Invert Pointer V`. See above.
- **A drag works but the drop does nothing.** The pointer left the surface during the drag. Folio keeps the drag alive while a button is held; check `Pointer Reach` covers where you stand and that nothing passes between you and the screen.
- **Hover and clicks land near what you aimed at, but not on it.** Usually the aiming rather than the mapping: rows a few percent of the screen tall are hard to hit from across a room. Check the cursor is on, then make the targets bigger.
- **Nothing hovers, and the document has a full-screen overlay.** An element covering the content takes the pointer even when decorative. Put `pointer-events: none` on scanlines, vignettes and any other overlay.
- **Typing goes to the game instead of the field.** The field does not have focus. Clicking it gives it focus, so the pointer has to work first.
- **Movement keys walk the player while a menu is up.** Call `Focus Document`. With only a text field focused, unclaimed keys fall through so the player can walk away mid-word.
- **Nothing at all, any mode.** `folio.WorldTest report` says whether the document recorded any geometry. Nothing recorded is a document problem (check the path and the log) rather than a rendering one.
- **It is enormous and the editor asserts in the RHI.** `Pixels Per Unit` times `World Size` past the device's texture limit. Folio scales it back and warns, but the document lays out smaller than asked.

# Documents in the world

A terminal on a wall, a sign, a screen you can walk up to. `UFolioWorldDocument` is a scene component
that loads a document and puts it on a surface in the level.

It is not a `UWidgetComponent`. That renders UI into a fixed-size render target and then samples it at
whatever size the quad lands on screen, so every distance except one is a filtered resample and text is
soft. Folio's text and vector art are distance fields, which means geometry can be drawn at the size it
is actually seen — no intermediate target, no resample, sharp at any distance.

## Getting one on screen

Add a **Folio World Document** component to an actor and set:

- **Document Path** — the same path `folio.Show` takes: relative to the project directory, so
  `ui/screens/terminal.rml`. A leading `/folio/` resolves inside the plugin instead.
- **World Size** — the physical size of the surface in world units, before the component's scale.
- **Pixels Per Unit** — how many document pixels each world unit is worth. This is the authoring
  scale, not a quality setting: at the default a 100x60 surface lays out as a 1000x600 document, so
  16dp text reads at about the size it would on a screen. Sharpness does not depend on it.

The surface lies in the component's local XY and faces along local Z, so it faces the way an actor
arrow does and you aim it by pointing the component at the viewer.

To see one without placing anything, in PIE:

```
folio.WorldTest ui/screens/terminal.rml
folio.WorldTest report
folio.WorldTest off
```

That spawns the document two metres in front of the camera, pitched up and turned off axis so
projection errors are obvious. Add `inscene` or `surface` to compare the modes.

## The three draw modes

`Draw Mode` decides which path picks the document up. The default for new components comes from
**Project Settings > Plugins > Folio > Default World Draw Mode**.

### After Tonemap (the default)

Drawn after the tonemapper, where temporal antialiasing can no longer touch it. This is the reason the
world path exists: TAA and TSR blur and smear world-space UI, worst of all while the camera moves, and
no amount of resolution fixes it. Colour also lands post-tonemap, so authored UI colours arrive as
authored, and under an upscaler the document is drawn at display resolution rather than render
resolution.

The price is that bloom, depth of field and motion blur never reach it, and antialiasing is Folio's to
produce — which is what the distance fields already do analytically. It is depth-tested against the
scene, so it goes behind geometry correctly.

Reach for this unless you have a specific reason not to.

### In Scene

Drawn into the scene as world-space triangles, before post-processing. Still no render target and no
resample, so still sharp at any distance, and it receives bloom, depth of field and colour grading like
anything else in the world. Temporal antialiasing will smear it.

For a diegetic screen that should sit *in* the world — a grimy monitor lit by the scene, a display that
should bloom.

### Surface

Rendered into an offscreen target for a material on a mesh to sample. See below.

This is the only mode with layer effects: `box-shadow`, `filter`, `backdrop-filter` and `mask-image`
all need somewhere offscreen to work in, and the whole document composites as one translucent unit. It
is also the only mode where the document can be on curved or non-planar geometry, because Folio no
longer decides where the pixels go.

The price is the one resample the other two modes exist to avoid, which is why it is not the default.

## Surface mode

Folio does not own the mesh. It renders the document into a `UTextureRenderTarget2D` and hands it to
you; putting that on something is the game's job.

**1. Set `Draw Mode` to `Surface`** on the component. Nothing appears until step 3 — that is expected,
not a failure. `folio.WorldTest report` will tell you what size it rendered at.

**2. Make a material instance of `M_FolioSurface`.** It ships in the plugin at `/Folio/M_FolioSurface`
and has one texture parameter, **`Element`**. Assign it to the mesh.

If you would rather build your own, or layer effects over the top, the settings that matter are:

| Setting | Value | Why |
| --- | --- | --- |
| Material Domain | Surface | It is a mesh in the world, not UI. |
| Blend Mode | **AlphaComposite (Premultiplied Alpha)** | Folio writes premultiplied RGB. Plain `Translucent` multiplies by opacity a second time and every edge goes dark. |
| Shading Model | Unlit | The document carries its own colour. |
| Texture sample | Sampler Type **Color** | The target is `RTF_RGBA8` and sRGB, so it decodes on sample. |
| | RGB → Emissive Color, A → Opacity | |

`M_FolioSurface` is single sided. Tick Two Sided on your instance if the surface can be seen from
behind.

**3. Point the material at the target.** Create a dynamic material instance on the mesh and set
`Element` from the component's **On Surface Changed** event.

The target is *replaced*, not resized, when the resolution changes, so a material given it once will
eventually be holding a stale texture. Bind the event rather than reading the target at Begin Play.

One wrinkle: with `Update In Editor` on, the first target is created before Begin Play, so the event
has already fired by the time your Blueprint binds to it. After binding, call **Get Render Target**
once and apply it if it is not null. Otherwise the mesh stays blank until the first resolution change.

That is the only thing `Get Render Target` is for — everywhere else, use the event.

### Resolution

The target is sized from how large the surface lands on screen, quantised to 128-pixel steps, so
walking up to the surface *sharpens* it rather than magnifying a fixed texture. Re-rendering at a new
size is cheap: geometry is compiled once and glyphs are size-independent, so nothing is re-atlased.

- **Max Surface Scale** bounds it, as a multiple of the document's own size. The default of 2 lets a
  1000x600 document reach 2000x1200.
- `folio.World.SurfaceAutoResolution 0` pins the target to the document's size instead, which is the
  fixed-resolution behaviour a `UWidgetComponent` has. Useful for an A/B.

Automatic sizing needs a player camera to measure against, so in the level viewport the target is
always the document's own size.

If the document comes up upside down, that is the mesh's UVs. Document V=0 is the top.

## Interacting with one

A document in the world is a real document: text fields, buttons, hover, focus and the caret all
behave as they do on screen. What it does not have is a widget, so nothing routes input to it by
default and you say where the pointer is.

To try it immediately, tick **Pointer From Player View**. The component then traces from the local
player's view each frame and drives the pointer itself, and **Pointer Key** (left mouse by default) is
the click. No input wiring, no Blueprint. `folio.WorldTest` turns this on for you.

That is a prototyping convenience and a fair fit for a game that aims with the camera. For a cursor, a
touch, or a hand in VR, leave it off and call the same path with your own ray:

| Call | Does |
| --- | --- |
| `Move Pointer Along (Origin, Direction, Distance)` | Puts the pointer where the line crosses the surface. Returns false and changes nothing if it misses, so one call answers both *is it being pointed at* and *where*. |
| `Move Pointer (UV)` | The primitive underneath, in 0-1 from the top left. Use it when the UV comes from somewhere Folio cannot work out, such as `Find Collision UV` against a curved mesh. |
| `Clear Pointer` | Takes the pointer off, so nothing is left hovered. |
| `Press Pointer` / `Release Pointer` | A click at wherever the pointer is. |
| `Scroll Pointer (Lines)` | Positive scrolls down. |

`Move Pointer Along` uses the **component's own rectangle**, not the mesh, so it works whatever you
modelled: place the component over the screen face with `World Size` matching it, and a bezel, a curve
or an oval cutout costs nothing. Only `Move Pointer` needs real UVs.

### Navigating it like a menu

Call **Focus Document** on the component and the d-pad, the arrows, WASD and IJKL all navigate it, the
same as any screen-space document. Directional input the document acts on stops reaching the game, so
the player works the terminal instead of walking while they do it. **Release Focus** hands input back.

Escape and the console key always fall through, and so does anything Folio has no meaning for.

Focus is one document at a time: focusing another releases the first.

A common shape is to focus on interact and release on Escape or on walking away. Nothing focuses a
world document on its own, deliberately: a screen the player has not chosen to use should not be taking
their movement keys.

### Typing

While a text field in the document has focus, the component takes Slate's keyboard focus through an
invisible widget, so characters arrive with the keyboard layout, modifiers and IME already resolved.
Deriving characters from key codes instead would be wrong for every layout but one.

Keys the document does not consume still fall through to the game, so movement keeps working. `Is
Taking Text` says whether the document currently has the keyboard, and `Release Focus` blurs the field
and hands it back. Give the player a way to call that: walking away from a terminal with a field still
focused means their keyboard is still going to it.

## Working in the editor

`Update In Editor` is on by default: a placed document loads, updates and draws in the level viewport,
which is the point of placing one. Turn it off for a document that should only exist in play, or one
whose animation is distracting to work next to.

A document only draws into the world it belongs to, so a level document does not appear in blueprint or
material preview viewports.

Markup and stylesheet edits hot reload in the world the same as anywhere else — no PIE restart.

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

- **Surface mode and the mesh is blank.** Nothing is sampling the target yet, or the Blueprint bound
  `On Surface Changed` and never received the first one. See step 3.
- **Every edge is dark, or the whole thing is dim.** The material is `Translucent` rather than
  `AlphaComposite`.
- **It is behind the wall it is mounted on.** Nudge it off the surface. `folio.World.Occlusion 0`
  confirms that is what is happening in one command.
- **Nothing hovers or clicks.** Nothing is driving the pointer. Tick `Pointer From Player View` or
  call `Move Pointer Along` yourself. Check `World Size` covers the screen face: the pointer is
  resolved against the component's rectangle, not the mesh.
- **Typing goes to the game instead of the field.** The field does not have focus. Clicking it is what
  gives it focus, so the pointer has to be working first.
- **Nothing at all, any mode.** `folio.WorldTest report` says whether the document recorded any
  geometry. Nothing recorded is a document problem — check the path and the log — rather than a
  rendering one.
- **It is enormous and the editor asserts in the RHI.** `Pixels Per Unit` times `World Size` past the
  device's texture limit. Folio scales it back and warns, but the document lays out smaller than asked.

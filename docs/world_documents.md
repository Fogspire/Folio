# Documents in the world

A terminal on a wall, a sign, a screen you can walk up to. `UFolioWorldDocument` is a scene component
that loads a document and puts it on a surface in the level.

It is not a `UWidgetComponent`. That renders UI into a fixed-size render target and then samples it at
whatever size the quad lands on screen, so every distance except one is a filtered resample and text is
soft. Folio's text and vector art are distance fields, which means geometry can be drawn at the size it
is actually seen — no intermediate target, no resample, sharp at any distance.

That is true of two of the three draw modes. **Surface mode does use an intermediate target, and is
softer for exactly the reason a `UWidgetComponent` is**, so pick it only when a document needs layer
effects or non-planar geometry.

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
It is **visibly softer than the other two in play**, on every document, and that is inherent - see
[Surface mode is softer](#surface-mode-is-softer-than-the-other-two-in-play-as-well).

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

The target is resized in place when the resolution changes, so the same object stays bound and a
material given it once keeps working. `On Surface Changed` still fires, for anything that tracks the
size rather than the pointer.

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
- Resolution rises the moment it is wanted and falls only after a whole step has been given up, so
  standing near a boundary does not flip it every frame.
- `folio.World.SurfaceAutoResolution 0` pins the target to the document's size instead, which is the
  fixed-resolution behaviour a `UWidgetComponent` has. Useful for an A/B.

Automatic sizing needs a player camera to measure against, so in the level viewport the target is
always the document's own size and looks softer than it will in play.

If the document comes up upside down, that is the mesh's UVs. Document V=0 is the top.

### Surface mode is softer than the other two, in play as well

Expect this, and expect it on every document rather than any particular one. **Surface mode is the
only mode that does not draw at the resolution the player sees**, and no amount of tuning removes
that entirely — it is what the mode costs.

The reason is what happens to the distance fields. Text and vector sprites carry their own edges and
resolve them at whatever resolution they are drawn at, which is why Folio is sharp under
`transform: scale(8)` and at 4K. `After Tonemap` and `In Scene` evaluate those fields **at final
pixels**: the glyph edge is computed for the pixel it lands in. Surface mode evaluates them into the
offscreen target, and a material then samples that texture onto a quad in perspective. The edge is
resolved once at the target's resolution and everything after it is filtering — so the field's whole
advantage is spent before the pixel is drawn, and the result behaves like a bitmap.

Texel and pixel also never line up. The quad is perspective-projected and the target is quantised to
128-pixel steps, so even a target that is the right size on average is being resampled at a slightly
wrong scale across most of the surface.

**Find out which limit you are against before tuning anything.** `folio.WorldTest report` prints the
target's size next to how tall the surface actually lands on screen, and says which of the two is the
constraint:

```
folio.WorldTest: 'ui/screens/terminal.rml' laid out at 1000x600, rendered into a 2048x1229 target,
                 covering 900 screen pixels tall
    the target is at or above the surface's on-screen size, so it is not what is limiting sharpness.
```

If the target is **smaller** than the on-screen size, raising `Max Surface Scale` will help. If it is
already at or above and still looks soft, more resolution will do nothing — the mesh is scene geometry,
so it is rasterised at the scene's rendering resolution and resolved through whatever temporal
upscaler the project runs. With TSR at 67%, a panel covering 900 display pixels is drawn into about
600 and upscaled back. **No target size fixes that**, and it is the usual answer.

What actually helps, in order:

1. **Use `After Tonemap` unless the document needs layer effects.** `box-shadow`, `filter`,
   `backdrop-filter` and `mask-image` are the whole reason to accept Surface mode. A document without
   them gives up sharpness for nothing.
2. **Raise `Max Surface Scale`.** The default of 2 is a memory bound, not a quality target. A panel
   the player walks right up to can easily want more than twice the document's size on screen, and it
   is clamped to this — so it magnifies. It goes to 4.
3. **Raise `Pixels Per Unit`** so the document is authored denser to begin with. This raises the
   *base* the scale multiplies, and it also re-lays-out the document, so check the design still fits.
4. **Confirm the material is not adding its own softness** — no mip bias, and a bilinear sample.

A layer also costs about `3/255` of brightness on the way through the target, from the 8-bit sRGB
round trip. Not visible except side by side, and unrelated to sharpness, but it is the other thing
Surface mode charges for.

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
| `Move Pointer To Hit (Hit)` | Uses the mesh's own UVs from a line trace. See below. |
| `Move Pointer (UV)` | The primitive underneath, in 0-1 from the top left. Use it when the UV comes from somewhere Folio cannot work out. |
| `Clear Pointer` | Takes the pointer off, so nothing is left hovered. |
| `Press Pointer` / `Release Pointer` | A click at wherever the pointer is. |
| `Scroll Pointer (Lines)` | Positive scrolls down. |

`Move Pointer Along` uses the **component's own rectangle**, not the mesh, so it works whatever you
modelled: place the component over the screen face with `World Size` matching it, and a bezel, a curve
or an oval cutout costs nothing.

### Getting the cursor to line up

There is a cursor drawn on the surface, on by default (**Show Cursor**). Nothing else tells you where
a click will land: the player is aiming a camera at a target a few percent of the screen across, and
hover alone is too late to aim by. Turn it off for a screen nobody points at.

Watch the cursor and fix what you see:

- **It moves the wrong way vertically** — tick **Invert Pointer V**. Your mesh's V runs up while a
  document's runs down. There is an `Invert Pointer U` for the same reason horizontally. This is the
  common one, and it is easy to miss because the *picture* looks perfect either way: a flipped V that
  the material corrects leaves the pointer reading the uncorrected one.
- **It drifts further off towards the edges** — the component's rectangle is not where the glass is.
  Check `World Size` and that the component sits centred on the screen face.
- **It does not move at all** — nothing is calling into the component. See the troubleshooting list.

`folio.World.ShowSurface 1` draws the rectangle the pointer resolves against and logs, per document,
the reason a ray missed, the pointer's position in document pixels, and the element under it as
`tag#id`. That last one settles a mis-click in one line: aim at a button, click, and the log says what
RmlUi thought you hit.

### Pointing by the mesh's UVs

**Pointer Uses Mesh UV** line traces the mesh and reads the UV of the triangle actually hit, rather
than intersecting the component's rectangle. More accurate, and correct by construction on a curved or
inset screen, where a flat rectangle only approximates the glass.

It costs a trace per frame and needs three things, so it is not the default:

1. **Support UV From Hit Results** on, in Project Settings > Physics (needs a restart)
2. Complex collision on the mesh, `Use Complex Collision As Simple` being the easiest
3. A `Pointer Trace Channel` the mesh blocks

If any is missing the log says so rather than pointing somewhere wrong.

**Pointer UV Channel** picks which UV set to read, for a mesh with more than one. Reading a different
channel than the material samples points somewhere else entirely.

The rectangle route is not a fallback — it is exact for a flat screen and needs no physics setup at
all. Reach for mesh UVs when the screen is genuinely curved, or when you want the extra precision.

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
  call `Move Pointer Along` yourself, and check `Pointer Reach` covers how far away you are standing.
  `folio.World.ShowSurface 1` says which of those it is.
- **The cursor moves the wrong way.** `Invert Pointer V`. See above.
- **A drag works but the drop does nothing.** The pointer left the surface during the drag. Folio
  keeps the drag alive while a button is held, but check `Pointer Reach` covers where you stand and
  that nothing passes between you and the screen.
- **Hover and clicks land near what you aimed at, but not on it.** Usually the aiming rather than the
  mapping: rows a few percent of the screen tall are hard to hit from across a room with no cursor.
  Check the cursor is on, then make the targets bigger. A document authored for reading is rarely one
  that can be aimed at.
- **Nothing hovers, and the document has a full-screen overlay.** An element covering the content
  takes the pointer even when it is only decorative. `pointer-events: none` on scanlines, vignettes
  and any other overlay.
- **Typing goes to the game instead of the field.** The field does not have focus. Clicking it is what
  gives it focus, so the pointer has to be working first.
- **Movement keys walk the player while a menu is up.** Call `Focus Document`. With only a text field
  focused, unclaimed keys deliberately fall through so the player can walk away mid-word.
- **Nothing at all, any mode.** `folio.WorldTest report` says whether the document recorded any
  geometry. Nothing recorded is a document problem — check the path and the log — rather than a
  rendering one.
- **It is enormous and the editor asserts in the RHI.** `Pixels Per Unit` times `World Size` past the
  device's texture limit. Folio scales it back and warns, but the document lays out smaller than asked.

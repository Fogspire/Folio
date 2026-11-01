# Documents in the world

A document on a surface in the level, a terminal or a sign the player walks up to and uses, sharp at any distance.

https://github.com/user-attachments/assets/19c0910f-c940-40cd-a8e8-3668150aa3a3

Every setting, draw mode and console command is in **[world document settings](world_document_settings.md)**.

## Put a document on a surface

1. Add a **Folio World Document** component to an actor.
2. Set **Document Path**, the same path `folio.Show` takes: relative to the project directory, so `UI/screens/terminal.rml`. A leading `/folio/` resolves inside the plugin instead.
3. Set **World Size** to the surface's size in world units, before the component's scale.
4. Point the component at the viewer: the surface lies in its local XY and faces along local Z.

It shows in the level viewport straight away (**Update In Editor** is on by default), and markup and stylesheet edits hot reload with no PIE restart. Leave **Draw Mode** on `After Tonemap` unless the document needs layer effects or a curved mesh; see [draw modes](world_document_settings.md#draw-modes).

To see a document without placing anything, in PIE:

```
folio.WorldTest UI/screens/terminal.rml
folio.WorldTest report
folio.WorldTest off
```

That spawns it 2 m in front of the camera, pitched up and turned off axis so projection errors show, with the player-view pointer on. Add `inscene` or `surface` to compare the modes.

## Show it on a mesh (Surface mode)

Surface mode renders the document into a `UTextureRenderTarget2D` for a material on your mesh to sample. It is the only mode with layer effects (`box-shadow`, `filter`, `backdrop-filter`, `mask-image`) and the only one for curved geometry, and it is softer than the other two.

1. **Set `Draw Mode` to `Surface`** on the component. Nothing appears until step 3.
2. **Make a material instance of `M_FolioSurface`** (`/Folio/M_FolioSurface`, one texture parameter, **`Element`**) and assign it to the mesh. Tick Two Sided on the instance if the surface can be seen from behind. To build your own material, see [the Surface material](world_document_settings.md#the-surface-material).
3. **Point the material at the target.** Create a dynamic material instance on the mesh and set `Element` from the component's **On Surface Changed** event. With `Update In Editor` on, that event has already fired by the time your Blueprint binds, so after binding call **Get Render Target** once and apply it if it is not null.

## Let the player point and click

Text fields, buttons, hover, focus and the caret behave as on screen, but nothing routes input to a world document until you say where the pointer is.

- **Camera aiming**: tick **Pointer From Player View**. The component traces from the local player's view each frame, and **Pointer Key** (left mouse by default) clicks. **Pointer Reach** is how far away the player can stand.
- **A cursor, a touch or a VR hand**: leave that off and call **Move Pointer Along** with your own ray each frame, then **Press Pointer** / **Release Pointer** for the click. It returns false if the ray misses, so one call answers both whether the screen is being pointed at and where.

Both intersect the **component's rectangle**, not the mesh, so place the component over the screen face with `World Size` matching it. For a curved or inset screen, point by the mesh's UVs instead: tick **Pointer Uses Mesh UV** and set up the three things it needs:

1. **Support UV From Hit Results** on, in Project Settings > Physics (needs a restart).
2. Complex collision on the mesh; `Use Complex Collision As Simple` is the easiest.
3. A **Pointer Trace Channel** the mesh blocks, and a **Pointer UV Channel** matching the one the material samples.

A cursor is drawn on the surface (**Show Cursor**, on by default) so the player can see where a click lands. Turn it off for a screen nobody points at.

## Navigate it like a menu, and type into it

Call **Focus Document** and the d-pad, arrows, WASD and IJKL navigate the document, while directional input it acts on stops reaching the game. Escape, the console key and anything Folio has no meaning for still fall through. **Release Focus** hands input back. A common shape is to focus on interact and release on Escape or on walking away; nothing focuses a world document on its own, and focusing one releases any other.

When a text field has focus, the component takes Slate's keyboard focus, so typing arrives with the keyboard layout, modifiers and IME resolved; keys the field does not consume still reach the game. **Is Taking Text** says whether it has the keyboard. Give the player a way to call **Release Focus**, or a field left focused keeps their keyboard.

## If it looks wrong

**Nothing at all, in any mode.** `folio.WorldTest report` says whether the document recorded any geometry. Nothing recorded is a document problem: check the path and the log.

**It is behind the wall it is mounted on.** It is depth-tested against the scene. Nudge it off the surface; `folio.World.Occlusion 0` confirms it.

**Surface mode and the mesh is blank.** Nothing is sampling the target, or the Blueprint bound `On Surface Changed` after it fired. See step 3 of [Surface mode](#show-it-on-a-mesh-surface-mode).

**Every edge is dark, or the whole thing is dim.** The material is `Translucent` rather than `AlphaComposite`.

**The document is upside down on the mesh.** Check the mesh's UVs: document V=0 is the top.

**Surface mode looks soft.** Expected to a degree: it is the only mode that does not draw at the resolution the player sees. Run `folio.WorldTest report`, which prints the target's size next to how tall the surface lands on screen:

```
folio.WorldTest: 'UI/screens/terminal.rml' laid out at 1000x600, rendered into a 2048x1229 target,
                 covering 900 screen pixels tall
    the target is at or above the surface's on-screen size, so it is not what is limiting sharpness.
```

- Target **smaller** than on screen: raise **Max Surface Scale** (default 2, up to 4).
- Target well **above** it: the surface is minified, and more resolution does not help.
- The two close and still soft: the mesh is scene geometry, rendered at the scene's resolution through the project's temporal upscaler. Use `After Tonemap` unless you need layer effects.

Raising **Pixels Per Unit** also helps, but re-lays-out the document, so check the design still fits. Make sure the material adds no mip bias and samples bilinear. In the level viewport there is no player camera, so the target stays at the document's own size and looks softer than in play.

**A fire or glow in front of it draws behind it.** Tick **Behind Translucency**, and check the effect's material is in the After DOF or After Motion Blur translucency pass. A document showing faintly through dim smoke over a dark scene is expected; raise an issue with a screenshot if it reads wrong.

**Nothing hovers or clicks.** Nothing is driving the pointer. Tick `Pointer From Player View` or call `Move Pointer Along`, and check `Pointer Reach` covers where you stand. `folio.World.ShowSurface 1` draws the rectangle the pointer resolves against and logs, per document, why a ray missed, the pointer's position in document pixels, and the element under it as `tag#id`.

**Nothing hovers, and the document has a full-screen overlay.** An element covering the content takes the pointer even when decorative. Put `pointer-events: none` on scanlines, vignettes and any other overlay.

**The cursor moves the wrong way vertically.** Tick **Invert Pointer V**: the mesh's V runs up while a document's runs down. The picture can look right either way, because a flip the material corrects is one the pointer does not see. `Invert Pointer U` does the same horizontally.

**The cursor drifts further off towards the edges.** The component's rectangle is not where the glass is. Check `World Size` and that the component sits centred on the screen face, or point by mesh UVs on a curved screen.

**With Pointer Uses Mesh UV on, nothing points.** One of its three requirements is missing, and the log says which.

**Hover and clicks land near what you aimed at, but not on it.** Usually the aiming: rows a few percent of the screen tall are hard to hit from across a room. Check the cursor is on, then make the targets bigger.

**A drag works but the drop does nothing.** The pointer left the surface during the drag. Check `Pointer Reach` covers where you stand and nothing passes between you and the screen.

**Typing goes to the game instead of the field.** The field does not have focus. Clicking it gives it focus, so the pointer has to work first.

**Movement keys walk the player while a menu is up.** Call `Focus Document`. With only a text field focused, unclaimed keys fall through so the player can walk away mid-word.

**It is enormous and the editor asserts in the RHI.** `Pixels Per Unit` times `World Size` is past the device's texture limit. Folio scales it back and warns, but the document lays out smaller than asked.

**It does not appear in a Blueprint or material preview viewport.** A document draws only into its own world. Look in the level viewport.

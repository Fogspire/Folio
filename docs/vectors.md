# Vector art

An icon can come from an SVG instead of a PNG, baked to the same kind of distance field the text
uses. Markup does not change: `<img sprite="icon_sword">` and `decorator: image(icon_sword)` work
exactly as they do for a raster sprite.

```bash
./bin/win64/msdfbake.exe vector --in art/ui/vector --out ui/art --rcss ui/vectors.rcss --layers
```

That reads the SVGs, packs the fields into an atlas, and writes a `@spritesheet` block between
markers in `ui/vectors.rcss`. Link that stylesheet and the sprites are available.

## When

Use it for art that has to survive **extreme magnification** - a HUD icon that also appears on a
full-screen map, anything on a world-space surface the player can walk up to, anything under a
`transform: scale` animation.

Everything else stays raster. A vector sprite costs one draw per colour layer where a whole raster
sheet costs one, and most icons are never seen at more than one size. See **[images](images.md)**.

## What a field can represent

Every rule below comes from one fact: **a distance field is coverage and nothing else.** One field is
one silhouette.

**Multi-colour art becomes a stack of silhouettes**, one per fill colour, painted back to front.
Still one atlas, still resolution independent, at the cost of one draw per layer. Keep the colour
count deliberately low.

**A stack needs its frame image.** Every layer is absolutely positioned and so adds nothing to its
container's height; on its own the container collapses and nothing renders. The baker emits a
`<name>_frame` sprite carrying the drawing's proportions, and a hidden `<img>` on it gives the box a
height:

```html
<div class="knight-stack" style="width: 240dp">
	<img class="knight-frame" sprite="knight_frame"/>
	<img class="knight_0-ink" sprite="knight_0"/>
	<img class="knight_1-ink" sprite="knight_1"/>
</div>
```

Set the width and let the frame find the height. RCSS has no `aspect-ratio`, and an image element's
intrinsic size is the only thing that carries one.

**Set width or height, not both.** They resolve against different bases - the parent's width and the
parent's height - so a stack sized as a percentage of both squashes whenever the window aspect
changes.

**`ninepatch` and `tiled-box` are invalid on a vector atlas.** Non-uniform stretching corrupts the
distance metric: corners round off and edges soften. A vector sprite scales uniformly or not at all.

**The vector sheet is declared `1x`.** There is no second resolution to author.

## Recolouring

Each layer is emitted with the colour the drawing used, as `image-color`. Override it and the art
recolours - which is the whole reason the stack exists rather than a flat bitmap:

```css
.knight_0-ink { image-color: #f0c674; }
.knight_1-ink { image-color: #1f1f28; }
```

That composes with themes for free. A silhouette stack is a palette you can repaint at runtime.

## Gradients

A gradient fill is baked to **colour** rather than to a field, onto a second sheet the baker writes
alongside the atlas. The field still produces the edge, so the silhouette stays exactly as crisp as
any other vector layer; only the fill is a raster.

That is the right trade rather than a compromise: a gradient is low-frequency and upscales almost
invisibly, where an edge is high-frequency and is the thing the field exists for. Zoom in on
`ui/examples/vector_gradient.rml` to see both halves of it.

Nothing changes in the markup - a gradient layer is a sprite like any other, and carries no
`image-color`, because it already has its colours. Setting one would multiply the gradient and flatten
the drawing to a tint of itself.

| Affinity fill | |
|---|---|
| Linear | works |
| Radial | works |
| Elliptical | works - it is a radial with a non-uniform transform |
| Conical | **outline only.** SVG has no conical gradient, and Affinity exports the fill as an embedded bitmap that the parser discards |
| Mesh | **outline only.** Same, and there is no mesh gradient in SVG to export to |
| Hatch | it is clipped geometry rather than a gradient, and bakes as flat layers |

For a conical or mesh fill, export it from Affinity as a PNG and use it as a raster sprite. It was
never going to be resolution independent - it is a bitmap in the SVG either way.

**The baker says so rather than leaving you to find out from the render.** It scans the source for
paints it cannot carry and names each one with what to do instead:

```
msdfbake: warning: conical: art/conical.svg contains <image>
msdfbake:   an embedded bitmap. Affinity exports a conical or mesh gradient this way, because SVG
msdfbake:   has neither. Export that fill as a PNG and use it as a raster sprite.
msdfbake:   Gradients that do bake: linear, radial and elliptical.
```

The same check catches `<pattern>` fills, SVG `<filter>` effects, `<meshgradient>`, and live
`<text>` that was never converted to curves. Pass **`--strict`** to make any of them fail the run,
which is what you want in a build.

## What the baker handles

- **`clip-path` is honoured.** nanosvg discards it, so the baker reads the relationship out of the
  SVG itself and intersects the geometry with Skia's path operations - the same library that unions a
  glyph's overlapping contours. A clip path's own shape is a stencil and is never drawn, which is
  what SVG says and what nanosvg gets wrong.
- **Strokes are outlined into area**, since a field has no notion of a line. Joins (miter with its
  limit, round, bevel) and caps (butt, round, square) are read from the SVG and honoured. A stroke
  costs its own layer, because it is its own colour.
- **Overlapping contours are resolved.** Art routinely draws a shape as several intersecting paths
  because a scanline fill does not care; a field does, and would reconstruct a false edge through the
  overlap. The baker unions them first.

## Baking is not automatic

**Re-run `msdfbake vector` after changing an SVG.** Nothing watches the source art.

And RmlUi caches a texture by its path and never looks at the file again, so a re-baked atlas keeps
rendering as it was. Reopening a document picks up anything whose timestamp moved;
`folio.ReloadTextures` does it without reopening. `uipreview` handles it through its file watcher.

## Tuning the bake

| | |
|---|---|
| `--px-em` | Field resolution per em. Higher is more precise and costs atlas area quadratically. |
| `--px-range` | The field's width in texels. Raise it only to widen the reach of effects. |
| `--page` | Atlas page size. One page per sheet - a spill is an error, because one `@spritesheet` names one image. |
| `--layers` | Bake multi-colour art as a stack. Without it a drawing is one silhouette and extra fills are discarded with a warning. |

`msdfbake dump --in <atlas> --png out.png` writes the field out as an image, which is how you check a
bake looks like the drawing rather than like soup.

## Where to go next

- **[Images and sprites](images.md)** — the raster path, which is still the default.
- **[Text and fonts](text_and_fonts.md)** — the same fields, for glyphs.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §7 — the full reference.

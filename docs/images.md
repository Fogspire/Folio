# Images and sprites

Raster art reaches a document one of two ways: an `<img>` element, or an `image` decorator on any element. Both name a **sprite** from a spritesheet rather than a file, so a screen is one texture and one draw.

`background-image` parses and does nothing.

## Spritesheets

```css
@spritesheet hud
{
	src: /UI/art/hud.png;
	resolution: 1x;

	icon_health:  0px   0px  48px 48px;
	icon_shield: 48px   0px  48px 48px;
	panel_frame:  0px  48px 128px 96px;
}
```

`Tools/uiatlas/pack_atlas.py` packs a directory of PNGs and writes that block between markers; the rest of the stylesheet is yours. Re-run it after adding art.

`resolution` lets one sheet serve several DPIs: declare a `2x` sheet and RmlUi halves the sprite's natural size, so `<img sprite="icon_health"/>` is 24dp either way.

## Using one

```html
<img sprite="icon_health"/>
<img src="/UI/art/loose.png"/>
<img src="/UI/art/sheet.png" rect="0 0 48 48"/>
```

An `<img>` has an **intrinsic size**, the only aspect ratio in RCSS (no `aspect-ratio` property exists). Set width *or* height and the other follows. Set both and it stretches. It is also how a vector layer stack gets a height; see **[layout](layout.md)**.

## The image decorator

For a surface rather than a picture: a panel background, a button skin, a bar fill.

```css
.panel  { decorator: image(panel_frame); }
.badge  { decorator: image(icon_star cover); }
.stripe { decorator: image(stripe repeat); }
```

Fit modes: `fill` (the default, stretch), `contain`, `cover`, `scale-none`, `scale-down`, `repeat`. These replace `object-fit`, which does not exist.

**The `repeat` family cannot name a sprite.** `repeat`, `repeat-x` and `repeat-y` draw one quad whose coordinates run to the tile count and leave the wrapping to the sampler, and a sub-rectangle of an atlas has nothing to wrap against: RmlUi refuses it in both renderers, with a warning. A repeating source is always its own file.

A tile is its texture's pixel size in **dp**, so it keeps its size across resolutions. A 960dp tile across a 1920dp screen draws the same thing twice, which reads as wallpaper for anything with recognisable features. For a soft, featureless surface, stretch one tile with `fill` and give the art the box's aspect ratio. `examples/tiling.rml` is the case in full.

**`ninepatch`** for anything that must stretch without distorting its corners, such as panels and buttons:

```css
.panel { decorator: ninepatch(panel_frame, panel_frame_inner); }
```

The second sprite names the inner rectangle; everything outside it is a fixed corner or edge.

**`tiled-horizontal`, `tiled-vertical`, `tiled-box`** tile a middle section between fixed ends. A `tiled-box` is a ninepatch that repeats rather than stretches.

**Multiple decorators paint in reverse source order**: the first named is on *top*, like CSS `background-image` layers. An effect listed after an opaque background is painted underneath it and appears to do nothing:

```css
/* The sheen is on top, where it was meant to be. */
.btn { decorator: shader("sweep #ffffffb0 0.35 0.3 15"), image(btn_base); }
```

## Colouring

`image-color` multiplies a sprite by a colour, so one white icon covers every state:

```css
.icon           { image-color: #8a8a99; }
.item:hover .icon { image-color: #e8e8f0; }
.icon.danger    { image-color: #e06c75; }
```

Author single-colour art white and tint it: one sprite instead of four, themeable, and `image-color` transitions.

## Gradients without art

Every gradient decorator works in both renderers: `gradient`, `horizontal-gradient`, `vertical-gradient`, `linear-gradient`, `repeating-linear-gradient`, `radial-gradient`, `repeating-radial-gradient`, `conic-gradient`, `repeating-conic-gradient`.

```css
.fade { decorator: vertical-gradient(#00000000 #000000c0); }
```

A gradient is cheaper than a PNG of a gradient, resolution independent, and one line to retune.

## When to use a PNG rather than a vector

Raster is the default. Use **[vector art](vectors.md)** only for something that has to survive extreme magnification, such as a HUD icon that also appears on a full-screen map, or a world-space surface a player can walk up to. Raster is one draw for the whole sheet; a vector costs a draw per colour layer.

Photographic or many-coloured art has no vector option: a distance field is coverage and nothing else.

## Hex colour, and the trap

**`rgba()`'s alpha channel is 0-255, not 0-1.** `rgba(0, 0, 0, 0.5)` parses to fully transparent.

Write `#0000007f`. `#rgb`, `#rgba`, `#rrggbb` and `#rrggbbaa` all work. `hsl()`, `hsla()`, `lab()`, `lch()`, `oklab()`, `oklch()` and the HTML colour names are accepted too.

## Textures the game provides

A scene capture, a rendered inventory item or a video frame has no path for a document to name. Register the texture and name it with the `folio://` scheme:

```cpp
Document->RegisterTexture(TEXT("minimap"), CaptureTarget);
```
```html
<img src="folio://minimap"/>
```
```css
.portrait { decorator: image(folio://inv/portrait); }
```

It works with `<img>`, every `image`-family decorator, `mask-image` and `image-color`. The texture is any `UTexture`: a render target, a streamed texture or a `UTexture2D` built at runtime.

**`UFolioWidget::RegisterTexture` releases its names when the widget is torn down.** `UFolioTextureLibrary` (Blueprint) and `Folio::RegisterTexture` (C++) reach the same registry for callers with no widget to hand, and those do not clean up after themselves.

Re-registering a name swaps the texture everywhere it is drawn, including in a document that is already up.

### Names are global

RmlUi caches a texture against its source string on a render manager, and Folio has one for the whole application. Two documents naming `portrait` get the same texture, whichever registered it last, and the swap is logged as a warning. Prefix by screen when that could happen (`hud/minimap`, `inv/portrait`), as with sprite names.

### It cannot exist in the preview

`uipreview` has no engine, so it draws a stand-in. Put a PNG at `UI/art/preview/<name>.png` and it draws that; leave it out and you get the usual named placeholder and one `I-ASSET-GAMETEXTURE` line. Either way the element occupies the right space.

**It is reported as info rather than a warning**, so a screen using a game texture still passes `--strict`.

### The capture behind it

**[A paper doll, and 3D items in a grid](tutorials/paper_doll.md)** covers isolating one without a staging area, lighting it apart from the level, the premultiply step a cutout needs, and driving a page of inventory cells from one rig.

## Missing art

An unresolved sprite renders as a magenta placeholder with its name stamped across it, and the name accumulates in `ASSETS_WANTED.md`. Screens can be built before any art exists, and the artist gets a list with the sizes each thing was drawn at.

`--strict` turns a missing sprite into a failing run: use it in CI, not while building.

## Re-baking

**RmlUi caches a texture by its path and never looks at the file again.** A re-packed atlas keeps rendering as it was until something releases it.

| | |
|---|---|
| Reopening a document | picks up anything whose timestamp moved |
| `folio.ReloadTextures` | the same, without reopening |
| `folio.ReloadTextures all` | releases every texture whether or not it looks changed |
| `uipreview` | handles it through its file watcher; needs nothing |

## Where to go next

- **[Vector art](vectors.md)**: when raster is not enough.
- **[Components and themes](components_and_themes.md)**, where the tint colours belong.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §5: the complete decorator vocabulary.

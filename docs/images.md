# Images and sprites

Raster art reaches a document one of two ways: an `<img>` element, or an `image` decorator on any element. Both name a **sprite** from a spritesheet rather than a file, so a screen is one texture and one draw rather than thirty of each.

There is no `background-image`. It parses and does nothing.

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

`Tools/uiatlas/pack_atlas.py` packs a directory of PNGs and writes that block between markers, so the sheet is generated and the rest of the stylesheet is yours. Re-run it after adding art.

`resolution` is what makes one sheet serve several DPIs: declare a `2x` sheet and RmlUi halves the sprite's natural size, so `<img sprite="icon_health"/>` is 24dp either way.

## Using one

```html
<img sprite="icon_health"/>
<img src="/UI/art/loose.png"/>
<img src="/UI/art/sheet.png" rect="0 0 48 48"/>
```

An `<img>` has an **intrinsic size**, and it is the only thing in RCSS that carries an aspect ratio - there is no `aspect-ratio` property. Set width *or* height and the other follows. Set both and it stretches.

That property is load-bearing elsewhere: it is how a vector layer stack gets a height, and how a ratio-preserving box is built at all. See **[layout](layout.md)**.

## The image decorator

For anything that is a surface rather than a picture - a panel background, a button skin, a bar fill.

```css
.panel  { decorator: image(panel_frame); }
.badge  { decorator: image(icon_star cover); }
.stripe { decorator: image(stripe repeat); }
```

Fit modes: `fill` (the default, stretch), `contain`, `cover`, `scale-none`, `scale-down`, `repeat`. That is the replacement for `object-fit`, which does not exist.

**The `repeat` family cannot name a sprite.** `repeat`, `repeat-x` and `repeat-y` draw one quad whose coordinates run to the tile count and leave the wrapping to the sampler, and a sub-rectangle of an atlas has nothing to wrap against - RmlUi refuses it in both renderers, with a warning. A repeating source is always its own file.

A tile is its texture's pixel size in **dp**, so it keeps its size across resolutions like everything else. Which also means the pattern repeats every tile: a 960dp tile across a 1920dp screen draws the same thing twice, and for anything with recognisable features that reads as wallpaper. Stretching one tile with `fill` never repeats, and for a soft, featureless surface it is usually the better answer - give the art the box's aspect ratio and nothing looks stretched. `examples/tiling.rml` is the case in full.

**`ninepatch`** for anything that must stretch without distorting its corners - which is most panels and every button:

```css
.panel { decorator: ninepatch(panel_frame, panel_frame_inner); }
```

The second sprite names the inner rectangle; everything outside it is a fixed corner or edge.

**`tiled-horizontal`, `tiled-vertical`, `tiled-box`** tile a middle section between fixed ends. A `tiled-box` is a ninepatch that repeats rather than stretches.

**Multiple decorators paint in reverse source order** - the first named is on *top*, exactly like CSS `background-image` layers. Written the intuitive way round, an effect listed after an opaque background is painted underneath it and appears to do nothing:

```css
/* The sheen is on top, where it was meant to be. */
.btn { decorator: shader("sweep #ffffffb0 0.35 0.3 15"), image(btn_base); }
```

## Colouring

`image-color` multiplies a sprite by a colour. That is how one white icon becomes every state:

```css
.icon           { image-color: #8a8a99; }
.item:hover .icon { image-color: #e8e8f0; }
.icon.danger    { image-color: #e06c75; }
```

Author single-colour art white and tint it. It is one sprite instead of four, it themes for free, and it animates - `image-color` is a colour, so it transitions.

## Gradients without art

Every gradient decorator works in both renderers: `gradient`, `horizontal-gradient`, `vertical-gradient`, `linear-gradient`, `repeating-linear-gradient`, `radial-gradient`, `repeating-radial-gradient`, `conic-gradient`, `repeating-conic-gradient`.

```css
.fade { decorator: vertical-gradient(#00000000 #000000c0); }
```

A gradient is cheaper than a PNG of a gradient, resolution independent, and one line to retune. Reach for art only when the shape is not expressible.

## When to use a PNG rather than a vector

Raster is the default and vector is the exception. Use **[vector art](vectors.md)** only for something that has to survive extreme magnification - a HUD icon that also appears on a full-screen map, or anything on a world-space surface a player can walk up to. Everything else is a sprite: raster is one draw for the whole sheet, where a vector costs a draw per colour layer.

Photographic or many-coloured art has no vector option at all - a distance field is coverage and nothing else.

## Hex colour, and the trap

**`rgba()`'s alpha channel is 0-255, not 0-1.** `rgba(0, 0, 0, 0.5)` parses to fully transparent.

Write `#0000007f`. Hex alpha is unambiguous, and `#rgb`, `#rgba`, `#rrggbb` and `#rrggbbaa` all work. `hsl()`, `hsla()`, `lab()`, `lch()`, `oklab()`, `oklch()` and the HTML colour names are accepted too.

## Textures the game provides

Everything above is a file. A scene capture behind a minimap, a paper doll, a rendered inventory item, a video frame - none of those is a file, and there is no path for a document to name.

Register the texture and name it with the `folio://` scheme:

```cpp
Document->RegisterTexture(TEXT("minimap"), CaptureTarget);
```
```html
<img src="folio://minimap"/>
```
```css
.portrait { decorator: image(folio://inv/portrait); }
```

It is a source like any other, so it works with `<img>`, every `image`-family decorator, `mask-image` and `image-color`. The texture is any `UTexture` - a render target is the usual one, but a streamed texture or a `UTexture2D` built at runtime works the same.

**`UFolioWidget::RegisterTexture` releases its names when the widget is torn down**, which is what you want for a screen. `UFolioTextureLibrary` (Blueprint) and `Folio::RegisterTexture` (C++) reach the same registry for callers with no widget to hand, and those do not clean up after themselves.

Re-registering a name swaps the texture everywhere it is drawn - the cached handle is released for you, so a document that is already up picks up the new one.

### Names are global

RmlUi caches a texture against its source string on a render manager, and Folio has one for the whole application. Two documents naming `portrait` get the same texture, whichever registered it last, and the swap is logged as a warning. Prefix by screen when that could happen - `hud/minimap`, `inv/portrait` - exactly as you would with sprite names.

### It cannot exist in the preview

There is no engine outside the engine, so `uipreview` draws a stand-in. Put a PNG at `UI/art/preview/<name>.png` and it draws that; leave it out and you get the usual named placeholder and one `I-ASSET-GAMETEXTURE` line. Either way the element occupies the right space and the rest of the layout is honest.

**It is reported as info rather than a warning**, so a screen using a game texture still passes `--strict`. A document naming one is correct, not incomplete.

### The capture behind it

Registering is one call; the capture that fills the target is where the work is. **[A paper doll, and 3D items in a grid](tutorials/paper_doll.md)** covers isolating one without a staging area, lighting it apart from the level, the premultiply step a cutout needs, and driving a page of inventory cells from one rig.

## Missing art

An unresolved sprite renders as a magenta placeholder with its name stamped across it, and the name accumulates in `ASSETS_WANTED.md`. That is deliberate: screens get built, navigated and animated before any art exists, and the artist gets a list with the sizes each thing was drawn at.

`--strict` turns a missing sprite into a failing run, which is what you want in CI and not while building.

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

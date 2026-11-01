# A helmet HUD that responds to the player

Build a first-person visor: a helmet frame around the view, a curved energy ribbon, a radar disc, a compass, a wireframe map, an ammo readout and a reticle, all of which trail the camera when the player turns and bob with their stride.

**No textures, no sprites, no vectors.** Every shape here is a box, a border, a gradient or a transform, so the whole thing is text you can edit and reload while the game runs.

- **A helmet is one element.** A rounded rectangle whose *border* is the helmet mass.
- **Curvature without 3D transforms**, which tilt planes but cannot bend them.
- **Motion is a component, not a transition**, and two of them drive different elements.
- **A HUD is not a screen**: one property decides whether the game works after an alt-tab.

```bash
./bin/win64/uipreview.exe UI/screens/visor.rml --root . --size 1920x1080 --strict
```

---

## 1. Turn input off first

The failure this prevents does not look like a UI bug.

```cpp
Document->bInteractive = false;
```

Leave a HUD interactive and the canvas is a focus target; Slate restores focus to a focusable widget when the window is reactivated, so the player alt-tabs away, comes back, and **can no longer move or turn**. Nothing is logged, and nothing about the HUD looks wrong.

Off makes the canvas unfocusable *and* hit-test invisible, so the keyboard and the mouse both reach the game. Painting, motion, data binding and hot reload are unaffected.

The other half of the same problem: the document must not paint over the world.

```css
body {
	background-color: #00000000;   /* not #000000 */
	width: 100%;
	height: 100%;
}
```

`background-color` on `body` is an opaque full-screen fill sitting under everything you drew. It is invisible in the preview, which composites onto black, and covers the entire game in engine. `folio.Surface.Shot` renders a document onto a transparent ground, which proves a HUD is see-through:

```
folio.Surface.Shot UI/screens/visor.rml C:/tmp/visor.png 1920 1080
```

Alpha inside the opening should be near zero. If it is 255, something is painting a background.

## 2. The helmet is a border

The visor opening is a rounded rectangle and the helmet surrounds it. Rather than four masses, make it **one element whose border is the mass**:

```css
#aperture {
	position: absolute;
	left: -400dp; top: -400dp; right: -400dp; bottom: -400dp;
	border-width: 400dp;
	border-color: #04070a;
	border-radius: 640dp 640dp 640dp 640dp;
}
```

The inner corner radius of a border is the outer radius **less the border width**, so `640 - 400` gives a 240dp opening radius. Pick the opening radius you want, add the border width, and that is the `border-radius`.

**`left` and `top` position the border box, not the content box.** With a 400dp border, an element at `left: 0` puts its border 400dp *off* the left of its parent and its opening starts at 0. That is why every inset here is `-400dp`.

**Percentage insets, not dp.** A dp scales with resolution and DPI, so a helmet defined in dp swallows the screen on a small window. Put the ring inside a box that is a *fraction* of the view and it covers the same share of the screen everywhere:

```css
.aperture-box {
	position: absolute;
	left: 7%; top: 4%; right: 7%; bottom: 7%;
}
```

The ring hangs off that box by its own border width in every direction, so the mass always reaches past the edge of the screen. A keyline and a bevel go in a second copy of the same box, painted after the readouts, so they sit on top:

```css
#aperture-key {
	position: absolute;
	box-sizing: border-box;
	left: 0dp; top: 0dp; right: 0dp; bottom: 0dp;
	border-width: 3dp;
	border-color: #2f9ad6b0;
	border-radius: 240dp 240dp 240dp 240dp;
	box-shadow: #2fa8ff30 0dp 0dp 26dp;
}
```

`box-sizing: border-box` goes on the keyline, not the ring. The keyline wants to sit *on* the opening edge; the ring wants its border to grow outward from it.

## 3. Anchor to the edge you belong to

A HUD is read at 16:9, at 21:9 and at 4:3. Anything positioned with `left` and a fixed width is wrong on two of those.

| Where it lives | How it is positioned |
|---|---|
| Top-left, bottom-left | `left`, plus `top` or `bottom` |
| Top-right, bottom-right | **`right`**, plus `top` or `bottom` |
| Centred | `left: 50%` with a negative `margin-left` of half its width |
| Spanning | `left` **and** `right`, no width |

RCSS resolves `left` and `right` together on an absolutely positioned element, so a bar across the bottom of the helmet is `left: 380dp; right: 380dp` and no width at all.

For the centred pieces, use percentages on both sides so the proportion holds:

```css
#energy {
	position: absolute;
	left: 50%;
	margin-left: -28%;
	width: 56%;
	top: 56dp;
}
```

With no `calc()` and no custom properties, the two numbers have to agree by hand. Get one wrong and the screen drifts off-centre as the window widens, so render at 2560x1080 to check.

## 4. Curvature, without 3D

**A 3D transform tilts a plane, and a visor is not a plane.** `perspective()` with `rotateY` turns a panel away from the eye, but nothing bends one, so a curve is faked in two dimensions.

The energy ribbon is a flex row of 41 ticks. Each tick is pushed down and rotated by an amount that grows with its distance from the middle, which is a parabola standing in for an arc:

```css
.ribbon {
	display: flex;
	flex-direction: row;
	justify-content: space-between;
}

.ribbon u { width: 14dp; height: 30dp; background-color: #46b6ee; }

.ribbon u:nth-child(20), .ribbon u:nth-child(22) { transform: translateY(0dp) rotate(0.7deg); }
.ribbon u:nth-child(15), .ribbon u:nth-child(27) { transform: translateY(2dp) rotate(4.2deg); }
.ribbon u:nth-child(10), .ribbon u:nth-child(32) { transform: translateY(7dp) rotate(7.7deg); }
.ribbon u:nth-child(1),  .ribbon u:nth-child(41) { transform: translateY(22dp) rotate(14deg); }
```

Pairs share a rule because the curve is symmetric; the left half then restates its own rules with the rotation negated, since the tangent leans the other way. It is verbose; the bow is what makes it read as a visor rather than a sticker.

Everything else gets a small `rotate()` toward the centre of the screen, so the corner clusters read as sitting on a dome:

```css
#radar  { transform: rotate(-5deg) scale(1, 0.78); transform-origin: 0dp 0dp; }
#visors { transform: rotate(5deg); }
```

`scale(1, 0.78)` squashes the whole radar, so the sweep and the rings inside it squash with it. `border-radius: 50%` on a wide box would make the outline an ellipse and leave its contents round.

## 5. Instruments out of gradients

The radar is five stacked discs, and none of them is art.

**A tick ring** is a repeating conic gradient with a hole cut in it by a mask:

```css
.disc-ticks {
	decorator: repeating-conic-gradient(from 0deg at 50% 50%,
		#bfeeffc0 0deg, #bfeeffc0 1.2deg, #00000000 1.2deg, #00000000 6deg);
	mask-image: radial-gradient(circle farthest-side at 50% 50%,
		#00000000 80%, #ffffffff 82%, #ffffffff 100%);
}
```

**A sweep** is a conic gradient that is transparent for most of its turn, animated on the element rather than in the decorator, because decorator arguments are not animatable:

```css
.disc-sweep {
	decorator: conic-gradient(from 0deg at 50% 50%,
		#7fe6ff00 0deg, #7fe6ff00 296deg, #7fe6ff40 354deg, #d6f6ff70 360deg);
	animation: 3.4s linear-in infinite radar-sweep;
}

@keyframes radar-sweep { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
```

**A triangle** does not exist either. Half of a rotated square, clipped by an in-flow parent, is an arrowhead:

```css
.self   { width: 46dp; height: 46dp; overflow: hidden; }
.self i { width: 24dp; height: 24dp; margin-left: 9dp; margin-top: 10dp; transform: rotate(45deg); }
```

**In-flow** matters: `overflow: hidden` does not clip an absolutely positioned child, so the inner square has to be a normal block for the parent to cut it in half.

**An arc** is a circle with two of its four border colours set to transparent, then rotated to taste. Per-edge `border-color` is supported even though `border-style` is not.

## 6. Motion is a component

A **transition** fires on a state change and runs a fixed curve to completion. A HUD trailing a camera responds continuously to a signal that never settles, which needs velocity carried between frames, and a stylesheet has nowhere to put velocity. Use `UFolioHudMotion`, and decide how the layers disagree:

| Element | Weight | Frequency | Damping |
|---|---|---|---|
| `m_frame` (the helmet) | -5, -3 | 4.5 | 0.90 |
| `m_trace` | -9, -6 | 3.4 | 0.85 |
| `m_tl` `m_tr` (corner clusters) | -18, -12 | 2.4 | 0.72 |
| `m_energy` (floating readout) | -26, -18 | 1.7 | 0.62 |
| `m_reticle` | -14, -10 | 2.8 | 0.75 |

Things further from the eye move more and settle slower; layers on identical springs move as one. Negative weights trail the motion, as a physical object does; positive leads it and reads as the HUD being eager.

The helmet barely moves and damps hardest, because it is bolted to the player's head. The reticle takes `Camera Turn` only, so a strafe does not slide the aim mark sideways.

**A screen-space HUD has to be named**, since a `UFolioWidget` lives in a widget tree rather than on an actor and there is nothing to search:

```cpp
Motion->SetDocumentSource(Document);
```

## 7. Footsteps are a second component

Camera turn and view-space velocity are what the motion component reads. A footstep cadence is neither: it is periodic, and a landing is an impulse. Both are a few lines of your own, and **they must drive different elements**.

```
#hud
  #bob_shell  -> #m_frame -> the helmet
  #bob_deck   -> #m_tl, #m_tr, #m_energy, ... -> every readout
  #bob_sight  -> #m_reticle
```

Two writers on one element's `transform` fight; nested elements compose. The motion component writes `m_*`, the bob writes `bob_*`, and RmlUi multiplies the matrices on the way down.

The bob itself is a phase advanced by ground speed, sideways once per stride and vertically twice:

```cpp
StridePhase += DeltaTime * StrideRate * SpeedRatio * UE_TWO_PI;

const float SwayX =  FMath::Sin(StridePhase) * StrideBlend;
const float SwayY = -FMath::Cos(StridePhase * 2.f) * StrideBlend;
```

`StrideBlend` ramps toward the speed ratio while grounded and toward zero in the air, so the bob fades rather than stopping dead when the player jumps.

A landing is an impulse into the spring's velocity rather than a target to chase, scaled by how hard the impact was:

```cpp
const float Impact = FMath::Clamp(-LastVerticalSpeed / ReferenceImpact, 0.f, 1.5f);
for (FLayer& Layer : Layers)
{
	Layer.Spring.Velocity.Y += LandKick * Impact * Layer.Impact;
}
```

`FFolioSpring` is public and does the integration: semi-implicit, substepped, so a hitch settles instead of throwing the HUD off screen. The vertical speed has to be remembered from the previous frame, because by the time a landing is detectable the velocity has already been zeroed.

Both components write `transform` and nothing else, and stop writing once a layer has settled, so a standing player's HUD goes idle and costs nothing.

## 8. Putting it on the screen

The HUD actor is the natural owner: it exists per player, its lifetime matches the game, and components on it tick without extra wiring.

```cpp
Widget = NewObject<UFolioWidget>(this);
Widget->DocumentPath = TEXT("UI/screens/visor.rml");
Widget->bInteractive = false;

SlateWidget = Widget->TakeWidget();
GetWorld()->GetGameViewport()->AddViewportWidgetContent(SlateWidget.ToSharedRef(), 0);

Motion->SetDocumentSource(Widget);
Bob->SetDocument(Widget);

// Mouse capture follows viewport focus. Adding overlay content can leave focus somewhere the
// viewport cannot recapture from, and the symptom is a camera that will not turn after an alt-tab.
FSlateApplication::Get().SetAllUserFocusToGameViewport();
```

Keep the components' tick disabled until the HUD is up, and disable it again when it comes down.

## 9. Bake the glow

The helmet's aperture is `#aperture-key`: a keyline and a 26dp glow over the whole opening. That glow is a **layer effect on an element covering most of the viewport**, which is a full-screen render target, allocated, cleared, drawn and composited. A HUD never idles, so that is one every frame.

`uipreview` says so:

```
warning: [W-PERF-FULLSCREENLAYER] #aperture-key: box-shadow on an element covering 77% of the
viewport costs a full-screen render target, every frame the document draws.
```

It applies to the radar's `mask-image` too if the disc grows large enough, and to any `filter` used the same way. The cost is not visible, hence the warning.

**Nothing about the glow changes at runtime, so make it a texture.** Split the document in two: the one that describes the effect, and the one that draws the result.

```html
<!-- UI/screens/visor_baker.rml -->
<body folio-bake-source>
	<div id="aperture-key" class="wash"/>
</body>
```

`folio-bake-source` on the `body` says this document exists to be baked, so the warning leaves it alone, and it carries no state classes: a running animation drives its own property and cancelling one for a bake is not reliable, so a layer caught mid-animation bakes empty. Then produce the art, from the editor console:

```
folio.Bake UI/screens/visor_baker.rml #aperture-key UI/art/visor_aperture.png 1920 1080
```

**The engine, not `uipreview`.** The two renderers do not draw `box-shadow`, `filter`, `backdrop-filter` or `mask-image` identically, so a layer effect baked in the preview is faint and the wrong shape in the game. `uipreview --bake` refuses those four and prints the line above instead; for anything else it is the quicker route.

And draw it, dropping the property and the radius it came from:

```css
#aperture-key {
	position: absolute;
	left: 0dp; top: 0dp; right: 0dp; bottom: 0dp;
	decorator: image(/UI/art/visor_aperture.png fill);
}
```

The border and the radius are in the texture. Everything that *moves* works untouched: section 6's sway is a `transform` and section 5's sweep is a rotation, and both are free.

In VSCode the warning carries the `folio.Bake` line for the element it flagged, with a button to copy it.

**Keep the baker.** It is the only readable description of the glow, and what you edit and re-bake when the blue is too strong.

What cannot be baked is anything whose shape depends on what is behind it. A `backdrop-filter` blurring the live scene through the visor is the example: it reads the frame underneath, and a texture cannot.

See **[performance and profiling](../performance_profiling.md)** for `--bake` in full, and **[the coloured screen edge](screen_edge.md)** for the same workflow on a document made entirely of washes.

## 10. Before you believe it

```bash
uipreview.exe UI/screens/visor.rml --root . --size 1920x1080 --strict --a11y
uipreview.exe UI/screens/visor.rml --root . --size 2560x1080 --strict
uipreview.exe UI/screens/visor.rml --root . --size 1600x1200 --strict
```

Three aspect ratios, because anchoring mistakes are invisible at the one you authored against.

Then look at it **in engine**. The two renderers are not identical, and over a live scene transparency, blending and draw order show problems a preview on black cannot.

## Where to go next

- **[HUD motion](../hud_motion.md)**: every property on the component, and how to tune a spring.
- **[Performance and profiling](../performance_profiling.md)**: what the layer passes cost, and `--bake`.
- **[Hosting a document](../hosting.md)**: `Interactive`, focus, and CommonUI.
- **[Layout](../layout.md)**: flexbox, and the traps.
- **[UI_CONVENTIONS.md](../../UI_CONVENTIONS.md)**: what exists. Read it before inventing a property.

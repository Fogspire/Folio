# A helmet HUD that responds to the player

A first-person visor: a helmet frame around the view, a curved energy ribbon, a radar, and readouts that trail the camera when the player turns and bob with their stride, built from boxes, borders, gradients and transforms with no art.

https://github.com/user-attachments/assets/6a28104d-f75e-4d6d-82de-1aaeb25269ce

> The video was made prior to Folio supporting real 3D transforms

You need Folio running in a project ([hello world](../hello_world.md)) and `uipreview` built. You write `UI/screens/visor.rml` and check each step with:

```bash
./bin/win64/uipreview.exe UI/screens/visor.rml --root . --size 1920x1080 --strict
```

## 1. Make the document see-through

Give `body` a transparent background:

```css
body {
	background-color: #00000000;   /* not #000000 */
	width: 100%;
	height: 100%;
}
```

The preview composites onto black, so an opaque body looks fine there and covers the whole game in engine. Render it onto a transparent ground from the editor console:

```
folio.Surface.Shot UI/screens/visor.rml C:/tmp/visor.png 1920 1080
```

Open the PNG: alpha inside the opening reads near zero. If it is 255, something is painting a background.

## 2. Draw the helmet as one border

The helmet is one element whose border is the mass:

```css
#aperture {
	position: absolute;
	left: -400dp; top: -400dp; right: -400dp; bottom: -400dp;
	border-width: 400dp;
	border-color: #04070a;
	border-radius: 640dp 640dp 640dp 640dp;
}
```

The inner radius is the outer radius less the border width, so `640 - 400` gives a 240dp opening. The insets are `-400dp` because `left` and `top` position the border box, not the content box.

Put the ring inside a box sized as a fraction of the view, so it covers the same share of the screen at every resolution:

```css
.aperture-box {
	position: absolute;
	left: 7%; top: 4%; right: 7%; bottom: 7%;
}
```

Add a keyline and glow as a second copy of the same box, placed after the readouts so it paints on top:

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

`box-sizing: border-box` goes on the keyline only, so it sits on the opening edge while the ring grows outward.

Run the preview: a dark helmet frame surrounds a rounded opening with a blue keyline and a soft glow on its edge.

## 3. Anchor each readout to its edge

| Where it lives | How it is positioned |
|---|---|
| Top-left, bottom-left | `left`, plus `top` or `bottom` |
| Top-right, bottom-right | **`right`**, plus `top` or `bottom` |
| Centred | `left: 50%` with a negative `margin-left` of half its width |
| Spanning | `left` **and** `right`, no width |

A bar across the bottom of the helmet is `left: 380dp; right: 380dp` and no width. For the centred energy readout, use percentages on both sides:

```css
#energy {
	position: absolute;
	left: 50%;
	margin-left: -28%;
	width: 56%;
	top: 56dp;
}
```

The two numbers have to agree; `calc()` can derive one from the other. Render at `--size 2560x1080`: the energy readout stays centred.

## 4. Bow the energy ribbon

A transform cannot bend a plane, so the curve is faked: the ribbon is a flex row of 41 ticks, each pushed down and rotated more the further it is from the middle.

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

Repeat the rules for the left half with the rotation negated, since its tangent leans the other way. Then tilt the corner clusters toward the centre of the screen:

```css
#radar  { transform: rotate(-5deg) scale(1, 0.78); transform-origin: 0dp 0dp; }
#visors { transform: rotate(5deg); }
```

`scale(1, 0.78)` squashes the whole radar, contents included. Run the preview: the ribbon bows down at both ends and the corner clusters lean in, as if on a dome.

## 5. Build the radar from gradients

The radar is stacked discs. A tick ring is a repeating conic gradient with its centre masked out:

```css
.disc-ticks {
	decorator: repeating-conic-gradient(from 0deg at 50% 50%,
		#bfeeffc0 0deg, #bfeeffc0 1.2deg, #00000000 1.2deg, #00000000 6deg);
	mask-image: radial-gradient(circle farthest-side at 50% 50%,
		#00000000 80%, #ffffffff 82%, #ffffffff 100%);
}
```

A sweep is a mostly transparent conic gradient, rotated on the element because decorator arguments are not animatable:

```css
.disc-sweep {
	decorator: conic-gradient(from 0deg at 50% 50%,
		#7fe6ff00 0deg, #7fe6ff00 296deg, #7fe6ff40 354deg, #d6f6ff70 360deg);
	animation: 3.4s linear-in infinite radar-sweep;
}

@keyframes radar-sweep { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
```

The player arrowhead is half of a rotated square, clipped by its parent:

```css
.self   { width: 46dp; height: 46dp; overflow: hidden; }
.self i { width: 24dp; height: 24dp; margin-left: 9dp; margin-top: 10dp; transform: rotate(45deg); }
```

Keep the inner square in flow: `overflow: hidden` does not clip an absolutely positioned child. For an arc, set two of a circle's four `border-color` edges to transparent and rotate it.

Run the preview: a squashed disc with ticks round its rim, a soft wedge of sweep, and an arrowhead at its centre.

## 6. Put it in the game

Create the widget from your HUD actor, with input off:

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

`Motion` is a `UFolioHudMotion` component on the HUD actor and `Bob` is your own component from step 8; add both now. `bInteractive = false` stops the HUD taking focus, which otherwise leaves the player unable to move after an alt-tab; see [a HUD is not a screen](../hosting.md#a-hud-is-not-a-screen). Keep the components' tick disabled until the HUD is up, and disable it again when it comes down.

Start PIE: the visor draws over the world. Alt-tab away and back, and you can still move and turn.

## 7. Make it trail the camera

Wrap each moving piece in an element with an `m_` id, and give the motion component a layer per element:

| Element | Weight | Frequency | Damping |
|---|---|---|---|
| `m_frame` (the helmet) | -5, -3 | 4.5 | 0.90 |
| `m_trace` | -9, -6 | 3.4 | 0.85 |
| `m_tl` `m_tr` (corner clusters) | -18, -12 | 2.4 | 0.72 |
| `m_energy` (floating readout) | -26, -18 | 1.7 | 0.62 |
| `m_reticle` | -14, -10 | 2.8 | 0.75 |

`m_reticle` takes `Camera Turn` only, so a strafe does not slide the aim mark sideways.

Turn hard in PIE: the helmet barely moves, the corner clusters and energy readout swing further and settle slower, and the HUD goes still when you stop. [HUD motion](../hud_motion.md) covers weight, frequency and damping.

## 8. Bob with the player's stride

Footsteps go on a second set of wrapper elements, because two writers on one element's `transform` fight while nested elements compose:

```
#hud
  #bob_shell  -> #m_frame -> the helmet
  #bob_deck   -> #m_tl, #m_tr, #m_energy, ... -> every readout
  #bob_sight  -> #m_reticle
```

In your bob component, advance a phase by ground speed, sideways once per stride and vertically twice:

```cpp
StridePhase += DeltaTime * StrideRate * SpeedRatio * UE_TWO_PI;

const float SwayX =  FMath::Sin(StridePhase) * StrideBlend;
const float SwayY = -FMath::Cos(StridePhase * 2.f) * StrideBlend;
```

Ramp `StrideBlend` toward the speed ratio while grounded and toward zero in the air, so the bob fades out on a jump. On landing, kick each layer's spring velocity by how hard the impact was:

```cpp
const float Impact = FMath::Clamp(-LastVerticalSpeed / ReferenceImpact, 0.f, 1.5f);
for (FLayer& Layer : Layers)
{
	Layer.Spring.Velocity.Y += LandKick * Impact * Layer.Impact;
}
```

`Layer.Spring` is an `FFolioSpring` ([using the spring on its own](../hud_motion.md#using-the-spring-on-its-own)). Remember the vertical speed from the previous frame: by the time a landing is detectable, the velocity is already zero. Write only `transform`, and stop writing once a layer has settled.

Walk in PIE: the HUD sways with each stride. Jump and land: it dips and settles.

## 9. Bake the glow

`uipreview` reports the keyline's glow, because a layer effect on an element this large costs a full-screen render target every frame:

```
UI/screens/visor.rml:0:0: info: [I-PERF-FULLSCREENLAYER] #aperture-key: box-shadow on an element covering 77% of the viewport costs a full-screen render target, every frame the document draws. ...
```

Nothing about the glow changes at runtime, so make it a texture. Create a baker document holding only the effect:

```html
<!-- UI/screens/visor_baker.rml -->
<body folio-bake-source>
	<div id="aperture-key" class="wash"/>
</body>
```

`folio-bake-source` keeps the report off it. Give its `body` no state classes: a layer caught mid-animation bakes empty. Bake it from the editor console:

```
folio.Bake UI/screens/visor_baker.rml #aperture-key UI/art/visor_aperture.png 1920 1080
```

Use the engine rather than `uipreview --bake`, which refuses `box-shadow`, `filter`, `backdrop-filter` and `mask-image` because the two renderers draw them differently.

In `visor.rml`, replace the keyline's border, radius and shadow with the texture:

```css
#aperture-key {
	position: absolute;
	left: 0dp; top: 0dp; right: 0dp; bottom: 0dp;
	decorator: image(/UI/art/visor_aperture.png fill);
}
```

Run the preview: the `I-PERF-FULLSCREENLAYER` line is gone and the visor looks the same, sway and sweep included. Keep the baker; it is what you edit and re-bake when the blue is too strong. [Performance and profiling](../performance_profiling.md#bake-it-in-the-engine) covers baking in full, including what cannot be baked.

## 10. Check three aspect ratios

```bash
uipreview.exe UI/screens/visor.rml --root . --size 1920x1080 --strict --a11y
uipreview.exe UI/screens/visor.rml --root . --size 2560x1080 --strict
uipreview.exe UI/screens/visor.rml --root . --size 1600x1200 --strict
```

All three show the readouts in their corners and the energy readout centred; anchoring mistakes are invisible at the ratio you authored against. Then look at it in engine, over a live scene, where transparency, blending and draw order can differ from the preview.

## Next

- **[HUD motion](../hud_motion.md)**: every property on the motion component.
- **[The coloured screen edge](screen_edge.md)**: the bake workflow on a document made of washes.

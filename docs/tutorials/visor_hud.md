# A helmet HUD that responds to the player

Build a first-person visor: a helmet frame around the view, a curved energy ribbon, a radar disc, a
compass, a wireframe map, an ammo readout and a reticle - all of which trail the camera when the
player turns and bob with their stride.

**No textures, no sprites, no vectors.** Every shape here is a box, a border, a gradient or a
transform, which means the whole thing is text you can edit and reload while the game runs.

The parts worth reading for themselves:

- **A helmet is one element.** A rounded rectangle whose *border* is the helmet mass.
- **Curvature without 3D transforms**, which RCSS does not have.
- **Motion is a component, not a transition**, and there are two of them driving different elements.
- **A HUD is not a screen**, which is one property and the difference between a game that works
  after an alt-tab and one that does not.

```bash
./bin/win64/uipreview.exe UI/screens/visor.rml --root . --size 1920x1080 --strict
```

---

## 1. Turn input off first

Do this before anything else, because the failure it prevents does not look like a UI bug.

```cpp
Document->bInteractive = false;
```

A HUD is a readout drawn over a game that is still being played. Leave it interactive and the canvas
is a focus target; Slate restores focus to a focusable widget when the window is reactivated, so the
player alt-tabs away, comes back, and **can no longer move or turn**. Nothing is logged. Nothing
about the HUD looks wrong.

Off makes the canvas unfocusable *and* hit-test invisible, so the keyboard and the mouse both reach
the game. Painting, motion, data binding and hot reload are unaffected.

The other half of the same problem: the document must not paint over the world.

```css
body {
	background-color: #00000000;   /* not #000000 */
	width: 100%;
	height: 100%;
}
```

`background-color` on `body` is an opaque full-screen fill sitting under everything you drew. It is
invisible in the preview - which composites onto black anyway - and covers the entire game in
engine. `folio.Surface.Shot` renders a document onto a transparent ground, so it is the quickest way
to prove a HUD is see-through:

```
folio.Surface.Shot UI/screens/visor.rml C:/tmp/visor.png 1920 1080
```

Alpha inside the opening should be near zero. If it is 255, something is painting a background.

## 2. The helmet is a border

The visor opening is a rounded rectangle. The helmet is what surrounds it. Rather than assembling
that from four masses, make it **one element whose border is the mass**:

```css
#aperture {
	position: absolute;
	left: -400dp; top: -400dp; right: -400dp; bottom: -400dp;
	border-width: 400dp;
	border-color: #04070a;
	border-radius: 640dp 640dp 640dp 640dp;
}
```

The inner corner radius of a border is the outer radius **less the border width**, so `640 - 400`
gives a 240dp opening radius. Choosing the pair is the whole trick: pick the opening radius you want,
add the border width, and that is the `border-radius`.

Two things about this that catch people out.

**`left` and `top` position the border box, not the content box.** With a 400dp border, an element at
`left: 0` puts its border 400dp *off* the left of its parent and its opening starts at 0. That is
standard CSS and it is still surprising at this scale - it is why every inset here is `-400dp`.

**Percentage insets, not dp.** A dp scales with resolution and DPI, so a helmet defined in dp swallows
the screen on a small window. Put the ring inside a box that is a *fraction* of the view and it
covers the same share of the screen everywhere:

```css
.aperture-box {
	position: absolute;
	left: 7%; top: 4%; right: 7%; bottom: 7%;
}
```

The ring hangs off that box by its own border width in every direction, so the mass always reaches
past the edge of the screen no matter how large the view gets. A keyline and a bevel go in a second
copy of the same box, painted after the readouts, so they sit on top:

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

Note `box-sizing: border-box` on the keyline and not on the ring. The keyline wants to sit *on* the
opening edge; the ring wants its border to grow outward from it.

## 3. Anchor to the edge you belong to

A HUD is read at 16:9, at 21:9 and at 4:3. Anything positioned with `left` and a fixed width is wrong
on two of those.

| Where it lives | How it is positioned |
|---|---|
| Top-left, bottom-left | `left`, plus `top` or `bottom` |
| Top-right, bottom-right | **`right`**, plus `top` or `bottom` |
| Centred | `left: 50%` with a negative `margin-left` of half its width |
| Spanning | `left` **and** `right`, no width |

RCSS resolves `left` and `right` together on an absolutely positioned element, so a bar across the
bottom of the helmet is `left: 380dp; right: 380dp` and no width at all.

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

There is no `calc()` and there are no custom properties, so the two numbers have to agree by hand.
Getting one wrong is a screen that drifts off-centre as the window widens, which is why it is worth
rendering at 2560x1080 before believing any of it.

## 4. Curvature, without 3D

**3D transforms do not exist.** No `perspective`, no `rotateX`, no `rotateY` - they parse and do
nothing. A curved visor has to be faked in two dimensions.

The energy ribbon is a flex row of 41 ticks. Each tick is pushed down and rotated by an amount that
grows with its distance from the middle, which is a parabola standing in for an arc:

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

Pairs share a rule because the curve is symmetric; the left half then restates its own rules with the
rotation negated, since the tangent leans the other way. It is verbose and it is worth it - the bow is
what sells a visor rather than a sticker.

Everything else gets a small `rotate()` toward the centre of the screen, so the corner clusters read
as though they are sitting on a dome:

```css
#radar  { transform: rotate(-5deg) scale(1, 0.78); transform-origin: 0dp 0dp; }
#visors { transform: rotate(5deg); }
```

`scale(1, 0.78)` is how you get an ellipse. `border-radius` takes lengths, not percentages, so a
circle is a square element with a radius larger than it needs - radii too large for their edge are
shrunk to fit - and an ellipse is that circle squashed on one axis.

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

**A sweep** is a conic gradient that is transparent for most of its turn, animated on the element
rather than in the decorator - decorator arguments are not animatable:

```css
.disc-sweep {
	decorator: conic-gradient(from 0deg at 50% 50%,
		#7fe6ff00 0deg, #7fe6ff00 296deg, #7fe6ff40 354deg, #d6f6ff70 360deg);
	animation: 3.4s linear-in infinite radar-sweep;
}

@keyframes radar-sweep { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
```

**A triangle** does not exist either. Half of a rotated square, clipped by an in-flow parent, is an
arrowhead:

```css
.self   { width: 46dp; height: 46dp; overflow: hidden; }
.self i { width: 24dp; height: 24dp; margin-left: 9dp; margin-top: 10dp; transform: rotate(45deg); }
```

**In-flow** matters: `overflow: hidden` does not clip an absolutely positioned child, so the inner
square has to be a normal block for the parent to cut it in half.

**An arc** is a circle with two of its four border colours set to transparent, then rotated to taste.
Per-edge `border-color` is supported even though `border-style` is not.

## 6. Motion is a component

A **transition** fires on a state change and runs a fixed curve to completion. A HUD trailing a
camera is responding continuously to a signal that never settles and gets kicked again before it
arrives, which needs velocity carried between frames. A stylesheet has nowhere to put velocity.

So it is `UFolioHudMotion`, and the only interesting decision is how the layers disagree:

| Element | Weight | Frequency | Damping |
|---|---|---|---|
| `m_frame` (the helmet) | -5, -3 | 4.5 | 0.90 |
| `m_trace` | -9, -6 | 3.4 | 0.85 |
| `m_tl` `m_tr` (corner clusters) | -18, -12 | 2.4 | 0.72 |
| `m_energy` (floating readout) | -26, -18 | 1.7 | 0.62 |
| `m_reticle` | -14, -10 | 2.8 | 0.75 |

Things further from the eye move more and settle slower. Three layers on identical springs is one
layer with extra steps. Negative weights trail the motion, which is what a physical object does;
positive leads it and reads as the HUD being eager.

The helmet barely moves and damps hardest, because it is bolted to the player's head. The reticle
takes `Camera Turn` only - a strafe should not slide the aim mark sideways.

**A screen-space HUD has to be named**, since a `UFolioWidget` lives in a widget tree rather than on
an actor and there is nothing to search:

```cpp
Motion->SetDocumentSource(Document);
```

## 7. Footsteps are a second component

Camera turn and view-space velocity are what the motion component reads. A footstep cadence is
neither: it is periodic, and a landing is an impulse. Both are a few lines of your own, and the
important part is that **they must drive different elements**.

```
#hud
  #bob_shell  -> #m_frame -> the helmet
  #bob_deck   -> #m_tl, #m_tr, #m_energy, ... -> every readout
  #bob_sight  -> #m_reticle
```

Two writers on one element's `transform` fight; nested elements compose for free. The motion
component writes `m_*`, the bob writes `bob_*`, and the browser - RmlUi, here - multiplies the
matrices on the way down.

The bob itself is a phase advanced by ground speed, sideways once per stride and vertically twice,
which is what a walk does:

```cpp
StridePhase += DeltaTime * StrideRate * SpeedRatio * UE_TWO_PI;

const float SwayX =  FMath::Sin(StridePhase) * StrideBlend;
const float SwayY = -FMath::Cos(StridePhase * 2.f) * StrideBlend;
```

`StrideBlend` ramps toward the speed ratio while grounded and toward zero in the air, so the bob
fades rather than stopping dead when the player jumps.

A landing is an impulse into the spring's velocity rather than a target to chase, scaled by how hard
the impact was:

```cpp
const float Impact = FMath::Clamp(-LastVerticalSpeed / ReferenceImpact, 0.f, 1.5f);
for (FLayer& Layer : Layers)
{
	Layer.Spring.Velocity.Y += LandKick * Impact * Layer.Impact;
}
```

`FFolioSpring` is public and does the integration - semi-implicit, substepped, so a hitch settles
instead of throwing the HUD off screen. The vertical speed has to be remembered from the previous
frame, because by the time a landing is detectable the velocity has already been zeroed.

Both components write `transform` and nothing else, and both stop writing once a layer has settled.
That is what lets a standing player's HUD go idle and cost nothing.

## 8. Putting it on the screen

The HUD actor is the natural owner: it exists per player, it already has a lifetime that matches the
game, and components on it tick without any extra wiring.

```cpp
Widget = NewObject<UFolioWidget>(this);
Widget->DocumentPath = TEXT("UI/screens/visor.rml");
Document->bInteractive = false;

SlateWidget = Widget->TakeWidget();
GetWorld()->GetGameViewport()->AddViewportWidgetContent(SlateWidget.ToSharedRef(), 0);

Motion->SetDocumentSource(Widget);
Bob->SetDocument(Widget);

// Mouse capture follows viewport focus. Adding overlay content can leave focus somewhere the
// viewport cannot recapture from, and the symptom is a camera that will not turn after an alt-tab.
FSlateApplication::Get().SetAllUserFocusToGameViewport();
```

Keep the components' tick disabled until the HUD is up, and disable it again when it comes down;
there is no reason to run two springs against a document that does not exist.

## 9. Before you believe it

```bash
uipreview.exe UI/screens/visor.rml --root . --size 1920x1080 --strict --a11y
uipreview.exe UI/screens/visor.rml --root . --size 2560x1080 --strict
uipreview.exe UI/screens/visor.rml --root . --size 1600x1200 --strict
```

Three aspect ratios, because anchoring mistakes are invisible at the one you authored against.

Then look at it **in engine**, not only in the preview. The two renderers implement the same
vocabulary but not identically, and a HUD is drawn over a live scene where transparency, blending and
draw order all matter in ways a preview on black cannot show you.

## Where to go next

- **[HUD motion](../hud_motion.md)** — every property on the component, and how to tune a spring.
- **[Hosting a document](../hosting.md)** — `Interactive`, focus, and CommonUI.
- **[Layout](../layout.md)** — flexbox, and the traps.
- **[UI_CONVENTIONS.md](../../UI_CONVENTIONS.md)** — what exists. Read it before inventing a property.

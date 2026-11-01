# A HUD that responds to the camera

A visor that lags a hard turn. A reticle that drifts and settles. A readout that sways with a run. A helmet with mass.

None of it is expressible in RCSS: a **transition** fires on a state change and runs a fixed curve to completion. This needs a continuous response to a signal that gets kicked again before it settles, which requires velocity carried between frames, and a stylesheet has nowhere to put velocity. So it is a component, and it drives one property.

## The shortest version

Put a **Folio HUD Motion** component on the actor that owns the document, and add a layer per element you want to move:

| | |
|---|---|
| `Element Id` | `visor_frame`: the id in the document, without the `#` |
| `Source` | `Camera Turn` |
| `Weight` | `-18, -12`: dp of travel at the reference rate |
| `Max Offset` | `40` |

The component finds the document on the same actor, so nothing else is wired.

**A screen-space HUD has to be named.** `UFolioWidget` sits in a widget tree, not on an actor, so hand it over once:

```cpp
Motion->SetDocumentSource(Document);
```

## Weight, and why it is negative

**Negative trails the motion, which is what a physical object does.** Turn right and a loosely mounted visor swings left before catching up. Positive leads the motion, which reads as eager rather than heavy.

`-18` means "eighteen dp of travel when turning at `Reference Turn Rate`". One pair of reference numbers retunes the whole HUD's sensitivity without touching a layer.

## Depth comes from disagreement

```
visor_frame    Weight -6,  -4    Frequency 4.0   Damping 0.9
reticle        Weight -14, -10   Frequency 2.5   Damping 0.75
readout        Weight -26, -18   Frequency 1.6   Damping 0.6
```

Things further from the eye move more and settle slower. That difference is the whole effect; three layers on identical springs look like one.

## Tuning the spring

**Frequency** is how quickly it responds, in oscillations per second. Around 2 feels attached; 6 or more is nearly rigid.

**Damping** is whether it overshoots. Below 1 overshoots and settles, 1 is the fastest approach with no overshoot, above 1 is sluggish.

**Slightly under 1 is what reads as physical.** A helmet that overshoots a little on a hard turn looks like it has mass; a critically damped one looks interpolated. Start at `0.7`; `0.9` is the most restraint worth having.

## What it costs

It writes `transform` and nothing else. A transform triggers no reflow, so driving one every frame is a matrix rather than a layout. Animating `left` here would make a HUD measurably slower than UMG; see **[animation](animation.md)**.

**It stops writing once a layer has settled**, so a still screen goes idle and costs nothing. Writing the same transform every frame would keep the document permanently invalidated. See **[performance](performance_profiling.md)**. A spring creeping through the last fraction of a dp counts as settled.

## Camera cuts

```cpp
Motion->SnapToRest();
```

Call it on a teleport or a camera cut. Without it the springs smear across the whole screen from wherever they were.

## Movement as well as turning

`Source` takes `Camera Turn`, `Movement`, or `Both`.

Movement is read in **view space**: how much of the pawn's velocity is sideways and how much vertical, from where the player is looking. A strafe sways the HUD sideways and a fall pushes it up.

## Using the spring on its own

`FFolioSpring` is a plain struct for anywhere a value should chase another with weight:

```cpp
// Two axes, always - a spring here is a point chasing a point. Drive one axis and ignore the other
// if that is all you need.
Spring.Advance(FVector2f(TargetX, TargetY), DeltaSeconds);
Element->SetProperty("transform",
    Folio::ToRmlString(FString::Printf(TEXT("translate(%.2fdp, %.2fdp)"),
        Spring.Value.X, Spring.Value.Y)));
```

It integrates semi-implicitly and substeps long frames, so a 200 ms hitch settles rather than diverging; an explicit integrator at that step size throws the value off screen.

## Where to go next

- **[Animation](animation.md)**: transitions and keyframes, which are the right tool for state changes and the wrong one for this.
- **[Nameplates and markers](markers.md)**: the other component that moves a document from game state.
- **[Documents in the world](world_documents.md)**: a helmet HUD that is genuinely in the scene.

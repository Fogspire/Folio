# A HUD that responds to the camera

A visor that lags a hard turn. A reticle that drifts and settles. A readout that sways with a run.
The Metroid Prime helmet.

None of it is expressible in RCSS, and it is worth being clear about why before reaching for the
component: a **transition** fires on a state change and runs a fixed curve to completion. What this
needs is a continuous response to a signal that never settles and gets kicked again before it
arrives, which requires velocity carried between frames. A stylesheet has nowhere to put velocity.

So it is a component, and it drives one property.

## The shortest version

Put a **Folio HUD Motion** component on the actor that owns the document, and add a layer per element
you want to move:

| | |
|---|---|
| `Element Id` | `visor_frame` — the id in the document, without the `#` |
| `Source` | `Camera Turn` |
| `Weight` | `-18, -12` — dp of travel at the reference rate |
| `Max Offset` | `40` |

That is the feature. The component finds the document on the same actor, so nothing else is wired.

## Weight, and why it is negative

**Negative trails the motion, which is what a physical object does.** Turn right and a loosely
mounted visor swings left before catching up. Positive leads the motion, which reads as the HUD being
eager rather than heavy, and is almost never what you want.

Weight is in dp at the reference rate, so `-18` means "eighteen dp of travel when turning at
`Reference Turn Rate`". One pair of reference numbers retunes the whole HUD's sensitivity without
touching a layer.

## Depth comes from disagreement

The reason to have three layers rather than one:

```
visor_frame    Weight -6,  -4    Frequency 4.0   Damping 0.9
reticle        Weight -14, -10   Frequency 2.5   Damping 0.75
readout        Weight -26, -18   Frequency 1.6   Damping 0.6
```

Things further from the eye move more and settle slower. That difference is the whole effect; three
layers on identical springs is one layer with extra steps.

## Tuning the spring

Two numbers, and they are the two anyone actually adjusts.

**Frequency** is how quickly it responds, in oscillations per second. Around 2 feels attached; 6 or
more is nearly rigid and barely worth a spring at all.

**Damping** is whether it overshoots. Below 1 overshoots and settles, 1 is the fastest approach with
no overshoot, above 1 is sluggish.

**Slightly under 1 is what reads as physical.** A helmet that overshoots a little on a hard turn
looks like it has mass; a critically damped one looks like it is being interpolated. `0.7` is a good
starting point and `0.9` is the most restraint worth having.

## What it costs

It writes `transform` and nothing else, deliberately. A transform triggers no reflow, so driving one
every frame is a matrix rather than a layout. Animating `left` here would make a HUD measurably
slower than the UMG it replaced - see **[animation](animation.md)**.

**It stops writing once a layer has settled**, so a still screen goes idle and costs nothing. That
matters more than it sounds: a component that wrote the same transform every frame would keep the
document permanently invalidated, and the invalidation path exists precisely so that a HUD nobody is
disturbing is free. See **[performance](performance_profiling.md)**.

A spring that is creeping through the last fraction of a dp is treated as settled, rather than kept
awake for a difference nobody can see.

## Camera cuts

```cpp
Motion->SnapToRest();
```

Call it on a teleport or a camera cut. Without it the springs travel across the whole screen from
wherever they were, which is a smear rather than a flourish.

## Movement as well as turning

`Source` takes `Camera Turn`, `Movement`, or `Both`.

Movement is read in **view space** - how much of the pawn's velocity is sideways and how much is
vertical, from where the player is looking. That is what makes a strafe sway the HUD sideways and a
fall push it up, rather than everything responding to a world-space direction that means nothing on
screen.

## Using the spring on its own

`FFolioSpring` is a plain struct and is useful anywhere a value should chase another with weight:

```cpp
// Two axes, always - a spring here is a point chasing a point. Drive one axis and ignore the other
// if that is all you need.
Spring.Advance(FVector2f(TargetX, TargetY), DeltaSeconds);
Element->SetProperty("transform",
    Folio::ToRmlString(FString::Printf(TEXT("translate(%.2fdp, %.2fdp)"),
        Spring.Value.X, Spring.Value.Y)));
```

It integrates semi-implicitly and substeps long frames, so a 200ms hitch settles rather than
diverging - an explicit integrator at that step size throws the value off screen exactly when the
game is already struggling.

## Where to go next

- **[Animation](animation.md)** — transitions and keyframes, which are the right tool for state
  changes and the wrong one for this.
- **[Nameplates and markers](markers.md)** — the other component that moves a document from game state.
- **[Documents in the world](world_documents.md)** — a helmet HUD that is genuinely in the scene.

# Nameplates and world markers

A document that lives on the screen but follows a place in the world: a name over a head, an
objective diamond, a damage number, an interaction prompt, a waypoint.

This is the other half of **[documents in the world](world_documents.md)**. That one puts a document
*in* the scene, where perspective and occlusion are the point - a monitor on a desk, a sign on a wall.
This one keeps the document square to the camera and perfectly crisp, and takes only its *position*
from the world.

Markers do not occlude and cannot be occluded. A nameplate behind a wall still draws, which is
usually what a marker is for; hiding one is the game's decision, and `SetVisibility` is how you make
it.

## The shortest version

Add a **Folio Marker Document** component to an actor, point it at a document, and that is the
feature:

| | |
|---|---|
| `Document Path` | `ui/hud/nameplate.rml` |
| `Size` | `256 x 96` (dp) |
| `Anchor` | `0.5, 1` - the bottom centre sits on the point |
| `Screen Offset` | `0, -16` - lift it clear of the head |

Move the component up to the head socket rather than offsetting in screen space where you can: an
offset in dp is the same at every distance, and a marker that should sit *on* something wants to move
with it.

`rcss/recipes/nameplate.rml` is a complete one - name, health bar, tail - ready to copy into your own
`ui/`.

## Distance

Off by default, everything here. A HUD marker that stays one size is easier to read than one that
does not, and most games want that.

**`Scale With Distance`** shrinks it the way something in the world would. `Reference Distance` is
where it draws at its authored size; `Min Scale` and `Max Scale` stop it becoming a dot or filling the
screen. The document is still laid out at its authored size and *then* scaled, so text never rewraps
as the player walks towards it - which is the thing that makes a naively built nameplate look broken.

**`Fade Start Distance` and `Fade End Distance`** fade it out and then stop drawing it. Both zero
switches this off. It matters more than it looks: thirty markers on a screen is noise, and distance is
the cheapest way to thin them.

**`Clamp To Screen Edge`** keeps an off-screen marker at the edge, in the direction of the thing it
labels. Right for objectives, wrong for nameplates - a clamped nameplate is a label stuck to the side
of the screen with no body under it. Behind the camera the projection mirrors through the origin, and
the component corrects for that, so a marker behind you is pinned to the correct edge rather than the
opposite one.

## Data

Same as anywhere else in Folio - bind an object and the document reads its properties:

```cpp
void AEnemy::BeginPlay()
{
    Super::BeginPlay();

    // Before the document loads. RmlUi resolves a document's bindings as it loads and offers no way
    // to attach a model afterwards, which is why this is not "bind whenever".
    Nameplate->BindViewModel(TEXT("nameplate"), NameplateModel);
    Nameplate->LoadDocument(TEXT("ui/hud/nameplate.rml"));
}
```

See **[data binding](data_binding.md)** for what the document side looks like. The nameplate recipe
reads `Name` and `Health`, and colours its bar from `Health` with `data-class-*` rather than from
code.

## What it costs

One document, one context and one draw per marker. That is not free, and thirty nameplates is thirty
contexts.

What makes it affordable is that an idle document is nearly free - the invalidation path skips
updating anything that has not changed - and that markers are usually idle. A nameplate whose health
has not moved does no layout work at all; it is repainted from geometry that is already compiled.

The number that actually hurts is *changing* markers. Thirty health bars all animating is thirty
layouts a frame. If that is your case, measure it with `stat Folio` before designing around it, and
see **[performance](performance_profiling.md)**.

Markers you cannot see cost nothing to draw and still cost their update. Use the fade distances.

## Doing it yourself

None of this is exotic, and if you want the placement under your own control the whole thing is:

```cpp
FVector2D Screen;
if (UGameplayStatics::ProjectWorldToScreen(Controller, WorldLocation, Screen))
{
    // ProjectWorldToScreen returns viewport pixels; a document is laid out in dp. Mixing the two is
    // the mistake that makes a HUD correct on one monitor and wrong on every other.
    const float Dpi = UWidgetLayoutLibrary::GetViewportScale(World);
    Slot->SetPosition(Screen / Dpi - Size * Anchor);
}
```

Put a `UFolioWidget` in a canvas panel and drive its slot with that. The component exists so that
every game does not write the projection, the culling, the scale curve and the fade again, and get the
DPI conversion subtly wrong each time - not because it is the only way.

## Where to go next

- **[Documents in the world](world_documents.md)** — when the document should be *in* the scene.
- **[Data binding](data_binding.md)** — driving a marker from gameplay.
- **[Hosting a document](hosting.md)** — the same document in a normal widget tree.

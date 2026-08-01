# A paper doll, and 3D items in a grid

A character rendered inside a profile frame, and an inventory grid whose cells are the items
themselves rather than icons. Both are the same trick: a scene capture, a name, and an `<img>`.

This tutorial assumes **[an inventory screen](inventory.md)**, because the grid half builds on it.

---

## What Folio does, and what it leaves to you

Folio does not render 3D. Unreal does, and there is no way around a `USceneCaptureComponent2D`
somewhere. What Folio removes is everything between the render target and the document: no material,
no `UImage`, no brush, no widget.

Register the texture under a name, and every source in the language accepts it:

```cpp
Widget->RegisterTexture(TEXT("doll/portrait"), Target);
```
```html
<img src="folio://doll/portrait"/>
```

So the interesting part of this tutorial is not the seam - that is one call - but the capture behind
it, which is where every paper doll goes wrong.

See **[images and sprites](../images.md)** §"Textures the game provides" for the seam in full.

---

## 1. Stop building a staging area

The usual advice is to put the mesh somewhere the player will never go - a room under the level, a
kilometre in the sky - so the main camera does not see it. Do not. There are two flags that do the
same job with no geometry and no coordinates to keep straight:

```cpp
Mesh->SetVisibleInSceneCaptureOnly(true);
```

`bVisibleInSceneCaptureOnly` renders the primitive in scene captures and nowhere else. The doll can
stand at the player's feet and the player will never see it.

The other half is stopping the capture seeing the world:

```cpp
Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
Capture->ShowOnlyActors.Add(this);
```

`PRM_UseShowOnlyList` renders *only* what is listed. Nothing else in the level reaches the target,
so there is no wall to hide behind and no skybox to crop out.

Together they are symmetrical: the doll is invisible to the world, and the world is invisible to the
doll. A staging area is what you build when you have neither.

**A far-away stage still has a use**: physics, cloth and hair sim that must not interact with level
geometry. If the doll has simulated parts, distance is still the honest answer.

---

## 2. Lighting it independently

`PRM_UseShowOnlyList` filters *primitives*. Lights are not primitives, so the level's lighting still
falls on the doll - which means a paper doll that goes dark when the player walks into a cave.

Lighting channels are the fix, and they cut both ways:

```cpp
constexpr bool bStage[3] = { false, true, false };

Mesh->SetLightingChannels(bStage[0], bStage[1], bStage[2]);
Key->SetLightingChannels(bStage[0], bStage[1], bStage[2]);
Fill->SetLightingChannels(bStage[0], bStage[1], bStage[2]);
Rim->SetLightingChannels(bStage[0], bStage[1], bStage[2]);
```

Everything in the level is on channel 0 by default. Moving the doll and its three lights to channel 1
means the level cannot light the doll, and the doll's key light cannot blow out the wall it is
standing next to.

Ambient arrives by other routes, so turn those off on the capture:

```cpp
Capture->ShowFlags.SetAtmosphere(false);
Capture->ShowFlags.SetFog(false);
Capture->ShowFlags.SetSkyLighting(false);
Capture->ShowFlags.SetGlobalIllumination(false);
Capture->ShowFlags.SetMotionBlur(false);
Capture->ShowFlags.SetEyeAdaptation(false);
```

**`EyeAdaptation` is the one people miss.** Left on, the doll's exposure drifts for a second every
time the panel opens, and it settles to a different brightness depending on what the player was
looking at. A portrait must look the same every time it is shown.

Three lights, once, is the whole rig: a key at about 30° off the camera axis, a dim fill opposite,
and a rim behind reading the silhouette out of the background.

---

## 3. The background, and the alpha trap

This is the part that costs a day. There are three routes and only two are worth taking.

### The easy one: an opaque backdrop

Capture `SCS_FinalColorLDR` against a backdrop plane behind the doll, and the target is opaque. The
document treats it as an ordinary picture, and the panel's shape comes from RCSS:

```css
.doll {
	width: 300dp;
	height: 400dp;
	border-radius: 8dp;
	decorator: image(folio://doll/portrait);
}
```

No alpha anywhere, nothing to get wrong. A gradient backdrop plane looks better than a flat one and
costs the same. **Start here** - it is what most profile frames actually want, and a cutout buys you
very little against a panel that has a background of its own.

### The real cutout: alpha output, then premultiply

For a doll that has to sit over the document's own background, you need alpha out of the capture.
That means enabling alpha propagation project-wide - Rendering settings, or
`r.PostProcessing.PropagateAlpha`, which is off by default - and:

```cpp
Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
Capture->bConsiderUnrenderedOpaquePixelAsFullyTranslucent = true;
```

That second flag keeps height fog off the pixels the doll did not cover, which otherwise fills the
cutout with a grey wash.

**Then premultiply, because Folio does not.** Every document draw goes through Slate as
`ESlateDrawEffect::PreMultipliedAlpha` - RmlUi composites premultiplied and Folio matches it. A file
texture is premultiplied as it loads; a registered `UTexture` is handed to the renderer exactly as
the game supplied it, and a scene capture writes *straight* alpha.

The symptom is a bright fringe around the whole silhouette, worst against a dark panel, and it reads
as a filtering or mip problem rather than a blending one. The fix is one material and one draw:

```cpp
// Doll -> capture target -> premultiply -> the target the document draws.
UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, DrawTarget, PremultiplyMaterial);
```

The material samples the capture target and outputs `RGB * A` with `A` unchanged, emissive, no
lighting. Register `DrawTarget`, not the capture's own target.

### The one to skip

`SCS_SceneColorHDR` writes **inverse** opacity into alpha, before tonemapping. Documented, and a
dead end here: Folio draws a texture as it finds it, so correcting it means the same extra material
pass as above, only now also doing the tonemap you skipped. If you are writing the material anyway,
enable alpha propagation and use `FinalColorLDR`.

---

## 4. One doll, on screen

```cpp
void AProfileDoll::BeginPlay()
{
	Super::BeginPlay();

	Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, 512, 768, RTF_RGBA8);
	Capture->TextureTarget = Target;

	Widget = NewObject<UFolioWidget>(this);
	Widget->DocumentPath = TEXT("UI/screens/profile.rml");
	Widget->RegisterTexture(TEXT("profile/doll"), Target);

	// ... hand Widget->TakeWidget() to the viewport or the game's widget tree
}
```

**Make the target the aspect ratio you want to draw at.** `<img>` is the only thing in RCSS carrying
an intrinsic size, and it takes it from the texture's surface dimensions - so a 512x768 target gives
the element a 2:3 ratio for free, and setting `width` alone gets the right height. A square target
stretched into a tall frame is the usual reason a doll looks slightly wrong and nobody can say why.

Then it is markup:

```html
<div class="profile">
	<img class="doll" src="folio://profile/doll"/>
	<h2>{{ Name }}</h2>
	<p class="t-caption c-dim">{{ Rank }}</p>
</div>
```

Prefix the name. **Names are global** - one registry for the application, because RmlUi caches a
texture against its source string - so two screens naming `doll` share whichever registered last.

---

## 5. Turning it

Rotate the mesh, never the camera. A camera orbit changes the lighting as it goes, and the rim light
you placed stops being a rim light.

```cpp
Mesh->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
```

Drive `Yaw` from the document. A drag on the frame is the direct gesture, and it is game code either
way, so send it out as an event:

```html
<img class="doll" src="folio://profile/doll" drag="drag"
	data-event-drag="Turn(ev.mouse_delta_x)"/>
```

For a pad, two shoulder buttons on a spring beat an analogue stick that never lets the doll settle
facing forward. Folio ships `UFolioHudMotion` for exactly that kind of value - see
**[hud motion](../hud_motion.md)**.

---

## 6. A grid of items, without a capture each

A profile frame is one capture. Twelve inventory cells rendering meshes are not twelve captures -
that is twelve full scene renders a frame for a screen where nothing moves.

Two things fix it, and you want both.

**Capture on demand.** The doll changes when the item changes, not every frame:

```cpp
Capture->bCaptureEveryFrame = false;
Capture->bCaptureOnMovement = false;
// ... after posing the mesh and the camera:
Capture->CaptureScene();
```

**Set the mesh to tick its pose regardless.** A skeletal mesh defaults to updating only when it was
rendered, and with `bCaptureEveryFrame` off it was not, so the first captured frame is a bind pose
and the second is a frame behind:

```cpp
Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
```

This is the single most confusing symptom in the whole feature. The item renders as an A-pose or a
stale frame, only in the grid, only sometimes, and it looks like a capture bug.

**One rig, N targets.** Keep one stage and one capture, and loop the visible items once:

```cpp
for (int32 Index = 0; Index < VisibleCount; ++Index)
{
	PoseFor(Items[Index]);
	Capture->TextureTarget = Targets[Index];
	Capture->CaptureScene();
	Widget->RegisterTexture(*FString::Printf(TEXT("inv/item%d"), Index), Targets[Index]);
}
```

```html
<div class="slot" data-for="entry : Slots">
	<img class="item" data-attr-src="'folio://inv/item' + it_index"/>
</div>
```

`data-attr-src`, not `src="{{ }}"` - interpolation is text content only.

Targets are the cost, not the names: 128x128 `RTF_RGBA8` is 64KB each, so a 48-cell page is 3MB and
fine. Pool them by the number of cells on screen rather than the number of items in the bag, and
re-register on scroll - **re-registering a name swaps the texture everywhere it is drawn**, and the
document picks it up without reloading.

Beyond a page or two of cells, bake the item shots offline into an atlas and use sprites. A rendered
item is worth its cost when it rotates, reflects the player's skin, or shows attachments; it is not
worth it for a static picture of a bolt.

---

## 7. What the preview draws

`uipreview` has no engine, so `folio://` cannot resolve. Put a PNG at `UI/art/preview/<name>.png` -
`UI/art/preview/profile/doll.png` for the name above - and the preview draws that. Leave it out and
you get the usual named placeholder.

It is reported as `I-ASSET-GAMETEXTURE`, info rather than a warning, so the screen still passes
`--strict`.

**Give the stand-in the target's aspect ratio.** The fallback placeholder is square, and §4's point
cuts both ways: an element sized by width alone takes its height from whatever texture it resolved,
so a 2:3 doll previews as a square and the panel below it sits 150dp too high. Screenshot the real
doll once, save it at the target's dimensions, and the preview and the engine agree - which is what
lets the panel be built, navigated and localised with the engine closed.

---

## 8. Check it

```bash
./bin/win64/uipreview.exe UI/screens/profile.rml --root . --strict --a11y
```

In the engine, the things worth looking at:

| | |
|---|---|
| A bright rim on the silhouette | straight alpha into a premultiplied blend (§3) |
| A-pose or a stale frame | `VisibilityBasedAnimTickOption` (§6) |
| Brightness drifts on open | `EyeAdaptation` (§2) |
| Doll goes dark in a cave | lighting channels (§2) |
| Squashed or slightly-off proportions | target aspect ratio versus element size (§4) |
| The other screen's doll appears | a name collision - prefix it (§4) |

---

## What you have learned

- `bVisibleInSceneCaptureOnly` and `PRM_UseShowOnlyList` instead of a staging area (§1)
- lighting channels, so the level and the stage cannot see each other's lights (§2)
- the three background routes, and why the cutout needs a premultiply pass (§3)
- that `<img>` takes its ratio from the target, so the target's shape is a layout decision (§4)
- capturing on demand, and the pose-tick flag that makes it work (§6)
- one rig and pooled targets for a grid, and when to stop and bake an atlas (§6)

## Where to go next

- **[Images and sprites](../images.md)** — the seam in full, and everything else a source can be.
- **[An inventory screen](inventory.md)** — the grid this bolts onto.
- **[Documents in the world](../world_documents.md)** — the other direction: a document *in* the 3D scene.
- **[Performance and profiling](../performance_profiling.md)** — what a screen costs once it is drawing captures.

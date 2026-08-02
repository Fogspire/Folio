# Supersampling

A document can be rasterised at more than one sample per pixel and filtered back down, which is the
only thing that helps a shallow diagonal: background antialiasing is one pixel of coverage, exact and
still stepping visibly on an edge that advances a pixel every six.

```cpp
Widget->Supersample = 2;   // 1 to 4, each way
```

Everything below is what that costs, what it must not change, and the three ways of getting it wrong
that all look like "supersampling makes it worse".

## The document does not scale

The sample count is a property of the **target**, not of the document. The document lays out, shapes,
breaks lines, bakes its glyph atlas and hit-tests in screen pixels at every sample count; the layers
it draws into are allocated denser, and the replay's transform stretches the one space onto the
other.

Scaling the document instead - multiplying the dp ratio by the sample count so it lays out into a
context twice the size - looks equivalent and is not:

- the font atlas is baked at the bigger size, so it costs four times the area and every glyph is
  resampled on the way to the screen;
- text metrics, line breaking and rounding all happen in a different space, so a line can break
  differently at a different sample count;
- hit testing, scroll offsets and any reported size are in that space too, and everything reading
  them has to know which one it is in.

Keeping the document in screen pixels means the only difference a sample count makes is how finely
the same geometry is rasterised, which is the whole point. A size report should therefore print the
same numbers whatever the sample count; if it does not, the document is being scaled somewhere.

The one thing that legitimately follows the target is a blur radius, which is expressed in document
pixels and applied to target pixels. Scale the sigma **and** the kernel's step: a kernel capped at a
tap count reaches half as far when the sigma doubles, and a wide shadow then ends in a visible edge
rather than fading out.

## Text does not want it

Glyphs are distance fields. They are exact at any size already, so a round trip through a denser
target and back can only soften them - visibly on a menu, unmissably on body copy. Supersampling text
pays for a resample it did not need.

So text is held out of the target: a glyph run drawn into the surface is recorded rather than
rasterised, and drawn to the screen after the composite, at one document pixel to one screen pixel.
Because the document never scaled, that is the same geometry, the same atlas and the same shader
constants an unsupersampled document uses, and the result is identical to the pixel.

Two things put a run back through the target, both because it belongs to something that reads the
target back:

- text inside a layer - a filter, a shadow, a mask, anything that composites as one image;
- text under a clip mask, which is a stencil the surface owns and the screen has no equivalent for.

Text something else paints over goes back too, or held-out text would be drawn on top of whatever
was meant to cover it. Test each draw as it arrives against the runs waiting, rather than assuming:

- test against what a layer **painted**, not the region it was allocated. A layer is sized to the
  current clip, which for an unclipped element is the whole screen, so allocating-region tests put
  every run on the screen back the first time anything casts a shadow;
- widen that by the blur, which is the one thing that puts colour outside the geometry that made it;
- split a covered run **per glyph**. A run is a whole line and its bounds are that line's, so a card
  overlapping the last word otherwise costs the sentence;
- put a run back where it arrived, with the clip it arrived under - by the time something is found to
  be covering it, the clip in force belongs to the coverer.

A document that ends with a full-screen sheen, scrim or vignette will keep all of its text in the
target, because all of it is painted over. That is the mechanism working.

## Downsample deliberately

**Do not draw the big target with a quad and let the sampler shrink it.** This is the trap that makes
supersampling look worse than no supersampling at all, and it is invisible in code review.

Hardware filtering averages the whole block only when the quad's texture coordinates land exactly on
texel corners, which needs the widget's rectangle to be a whole number of pixels, at exactly the
sample count, at an integer position. Anywhere else - a fractional window size, a rounded context
size, a half-pixel offset - each output pixel reads *one* texel of an unfiltered raster. That is a
nearest-neighbour decimation: edges come out harder than they would have without supersampling,
uniformly across the screen, because the phase error is the same everywhere.

Instead, average the block in a pass of your own: point-sample the N×N texels behind each destination
pixel and divide. Then composite that one-sample-per-pixel image with a quad snapped to whole pixels,
so the last step is a copy rather than a second resample.

Measured on one screen, counting how many columns crossing a shallow diagonal have no intermediate
pixel at all - a hard step:

| | hard columns |
|---|---|
| no supersampling | 72 |
| 2×, composited through the sampler | 62 |
| 2×, resolved in a pass | 26 |
| 4×, resolved in a pass | 21 |

The middle row is the trap: four samples per pixel bought almost nothing, and the effort went into
producing a picture that was then thrown away one texel at a time.

## Keep the skirt in screen pixels

A background carries its own antialiasing as geometry - a one pixel skirt of coverage on its outer
boundary. It is tempting to express that in *rendered* pixels, so a supersampled document gets a
skirt one target pixel wide and the samples do the rest.

Do not. Keeping the skirt one **screen** pixel wide at every sample count means the geometry is
identical however densely it is rasterised, so the samples add to the skirt instead of replacing it,
and supersampling can never come out harder than not supersampling. It also means the sample count
can change without regenerating any geometry - and since backgrounds are generated once and cached,
regenerating them means a restyle, which replays entry animations and makes a settings toggle look
like a screen reload.

## What it costs

- A target of the viewport times the sample count, squared, plus a one-sample-per-pixel target to
  resolve into. At 4× that is sixteen times the pixels: measure before offering it.
- Every layer the document pushes is allocated at the same density, so a shadow at 4× is a sixteen
  times bigger allocation than it looks. Layer pools hold those between frames.
- A depth-stencil the size of the layer, if the replay allocates one per resolve. Only a clip mask
  needs it; check the recorded draws and skip it otherwise, or a document with no masks at all pays
  a full-size depth-stencil per layer per frame.
- One resolve pass and one composite per frame.

So it is a per-document choice: on for a screen of slanted panels, off for one that is mostly text,
and worth measuring at 4× before assuming it is simply "more".

## Checking it

Report, per document: the sample count, the size it laid out against, and how many glyph runs the
last paint drew natively. Zero native runs on a screen with words on it means something is covering
all of them - usually a full-screen overlay, occasionally a bounds test that is too coarse.

Offer a switch that puts text back through the target, so the two can be compared in place. The
useful comparison is not "does supersampling look good" but "is the text identical to the same
document unsupersampled, and are the edges softer" - both of which can be measured from two
screenshots rather than argued about.

## Measuring it

The cost is not where the code that queues it lives. A layer is replayed, blurred, masked and resolved
on the **render thread**, and a profiling scope on the game-thread function that queued the work ends
at the enqueue - so a layer costing milliseconds reports as microseconds and the UI looks free while
the frame is anything but. Put a scope and a cycle counter *inside* every render-thread lambda, and a
GPU stat scope on the graph builder, or none of it is attributable.

Report the memory too. Layers are allocated at the sample count squared and pooled between frames,
so the number that decides whether a sample count is affordable is the total the pool holds; nothing
else on screen shows it. Print the target size and its megabytes beside the sample count.

Worth knowing before optimising: on one screen at four samples each way, the whole of the UI came to
about a millisecond of GPU time against a twenty millisecond scene, and the layer pool held 316 MB at
a 1915x1270 context. Supersampling is fill and memory, and both scale with the square of the sample
count *and* with the resolution - four samples at 4K is sixteen times a 33 megapixel document. Measure
before assuming the cost is where it feels like it should be.

## Skip the frame that changed nothing

A supersampled document renders into a target that survives the frame, which the direct path does not
have. So a frame in which the document did not change does not need rendering again: composite the
target that is already there, and draw only the text, which was never in it.

That needs the layout to report whether it actually updated, and the held-back runs to be kept from
the last render rather than dropped. It is the difference between a still screen costing the sample
count squared every frame and costing it once - but it only helps documents that go idle. One that
polls a data model every frame, or animates, is dirty every frame by definition and pays in full.

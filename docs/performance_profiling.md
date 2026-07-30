# Performance and profiling

Folio reports itself through Unreal's ordinary tools. There is no separate profiler to learn: `stat`,
Unreal Insights, CSV captures and LLM all know about it.

## The quick look: `stat Folio`

```
stat Folio
```

An on-screen breakdown of where Folio's frame goes:

| Row | What it covers |
|---|---|
| **Update** | RmlUi's own work: style resolution, layout, data model updates, animations |
| **Render** | Turning the document into Slate draw elements |
| **Compile geometry** | Building a mesh RmlUi will reuse. Should be rare after a screen settles |
| **Draw geometry** | Emitting one compiled mesh. Called a lot; each call should be tiny |
| **Load texture** | Decoding art. Should be zero except when a screen first opens |
| **Resolve layers** | The render-target passes behind `filter`, `box-shadow` and `backdrop-filter` |
| **Draws** | Draw calls this frame |
| **Geometry batches** | Batches this frame |

**What to look at first.** `Draws` and `Geometry batches` climbing is the usual cause of a slow screen,
and it is almost always texture switches — art that should be in one atlas spread across several. That
is a packing problem, not a renderer problem.

**Compile geometry** repeating every frame means something is invalidating geometry that should be
stable. Animating `width`, `height`, `margin` or `padding` does exactly this, because those change
layout. Animate `transform`, `opacity` and colour instead; that is the single biggest authoring
decision affecting Folio's cost.

Also useful, and Slate's rather than ours:

```
stat Slate
```

Its batch count is the honest measure of how well Folio's output is compositing with everything else.

## Unreal Insights

Folio has its own trace channel, so a capture can ask for it without everything else:

```
-trace=default,folio
```

Every scope in `stat Folio` appears in the timeline, including the ones inside the portable core — the
distance field generator names its own scopes through a hook table, so glyph generation shows up as
Folio work rather than as an unexplained gap.

For a CSV capture the category is `Folio`. For memory, `-llm` attributes the glyph atlases and RmlUi's
own allocations to a Folio tag, so they read as Folio rather than as untagged texture memory.

## Idle cost: a document that has not changed does nothing

This is the thing most likely to surprise you, so it is worth understanding rather than trusting.

A naive integration runs RmlUi's update and render every frame for every open document, whether
anything changed or not. Folio does not: it asks RmlUi when the document next needs attention, and
skips the frame entirely until then. A static pause menu costs approximately nothing.

`folio.Invalidation` controls it and is **on by default**.

The signal comes from RmlUi itself, which tracks the cases that are easy to get wrong: a running
`animation` or `transition`, a blinking caret, a held scroll arrow, a smooth scroll. Folio adds the
ones on its own side of the boundary: input, loading a document, a resize, a data model change, a
stylesheet reload.

### When a document seems frozen

The failure mode of this feature is a document that stops updating, and it presents as something small
— the caret stopped blinking, a hover state stuck, a HUD number stopped moving.

**First, prove whether invalidation is the cause:**

```
folio.Invalidation 0
```

If the symptom goes away, the dirty tracking missed a change. If it does not, invalidation is innocent
and the problem is elsewhere.

**Then find out what Folio thinks:**

```
folio.Invalidation.Trace 1
```

Each update logs why it happened and when RmlUi wants the next one:

```
folio invalidation: updated because 'mouse move', next update in never, until input
folio invalidation: updated because 'data model', next update in 0.694s
folio invalidation: updated because 'RmlUi asked', next update in 0.000s
```

Read it like this:

- **`never, until input`** — the document is genuinely static. Correct for a settled menu.
- **`0.000s`** — something is animating, so every frame counts. Correct while a transition runs.
- **a small delay** — a timer, almost always a caret.
- **no lines at all after the first few** — the document is idle. If it should not be, something
  changed it without saying so.

If you change a document from game code in a way Folio cannot see — reaching into the RmlUi DOM
directly rather than through a data model — tell it:

```cpp
Canvas->MarkDirty(TEXT("my own reason"));
```

The reason is what the trace prints, so make it name your code.

### Checking it still works

```bash
./scripts/check_invalidation.sh
```

Runs a real editor against three documents and asserts **both directions**, because a build where
everything idles forever would pass a check that only tested idling:

- a static document updates a handful of times and then stops
- a document with a running animation updates every frame
- a document with a focused, blinking caret updates on the blink timer

Worth running after anything that touches input, data binding or document loading — those are the
paths that raise the dirty flag, and a missed one is a frozen document.

## If a screen is slow

In order of likelihood, and check these before concluding anything structural:

1. **Geometry re-uploaded every frame.** Watch `Compile geometry` in `stat Folio`. Usually caused by
   animating a layout property.
2. **Animation on the wrong properties.** `width`, `height`, `margin`, `padding` trigger reflow.
   `transform`, `opacity` and colour do not.
3. **Poor batching from texture switches.** `Draws` and `Geometry batches` tell you; the fix is
   atlasing more aggressively rather than anything in code.
4. **Layer effects.** `filter`, `backdrop-filter` and `box-shadow` each need an offscreen pass, which
   `Resolve layers` measures. They are not free and a screen using several is paying for each.
5. **A polled data model.** `BindViewModel(..., bPollEveryFrame = true)` re-reads every bound field
   every frame, and it also stops the document from ever idling. A source implementing
   `INotifyFieldValueChanged` — which `UMVVMViewModelBase` does — updates on change instead, which is
   the reason to use one.

## Text is not the expensive part

Worth saying because it is a natural suspicion. Glyphs are size-independent distance fields, so text at
a new size costs no atlas work, and `transform: scale` on text costs nothing at all. Generating a glyph
the first time is real work and shows up under **Load texture** and in the font report:

```
folio.Font.Report
```

Faces, variation tuples, page and glyph counts, cache hits and misses. Misses climbing during play
means glyphs are being generated at runtime; the fix is prewarming the charset, not making text cheaper.

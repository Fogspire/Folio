# Profiling reference

Every number Folio reports to Unreal's `stat`, Insights, CSV and LLM tools, and what each one means. For finding and fixing a slow screen, see [performance and profiling](performance_profiling.md).

## `stat Folio` rows

| Row | What it covers |
|---|---|
| **Update** | RmlUi's own work: style resolution, layout, data model updates, animations |
| **Render** | Turning the document into Slate draw elements |
| **Compile geometry** | Building a mesh RmlUi reuses. Should be rare after a screen settles |
| **Draw geometry** | Emitting one compiled mesh. Called a lot; each call should be tiny |
| **Load texture** | Decoding art and generating glyphs. Should be zero except when a screen first opens |
| **Resolve layers** | The render-target passes behind `filter`, `box-shadow` and `backdrop-filter` |
| **RT: replay layer** | Replaying one layer's draws on the render thread. Broken down by **RT: layer graph**, **RT: layer buffers** and **RT: layer state** |
| **Draws** | Draw calls this frame |
| **Geometry batches** | Batches this frame |
| **Layers drawn** | Layers rendered this frame |
| **Layers reused** | Layers whose contents had not changed, so the picture from an earlier frame was drawn again |

`stat Slate` gives the batch count for the whole UI, Folio included.

## Insights, CSV and memory

Folio has its own trace channel, so a capture can ask for it without everything else:

```
-trace=default,folio
```

Every scope in `stat Folio` appears in the timeline, including those inside the portable core, so glyph generation shows up as Folio work rather than an unexplained gap.

For a CSV capture the category is `Folio`. For memory, `-llm` attributes the glyph atlases and RmlUi's allocations to a `Folio` tag rather than untagged texture memory.

## Reading the invalidation trace

`folio.Invalidation` skips the update and render of a document that has not changed, and is on by default. `folio.Invalidation.Trace 1` logs each update, why it happened and when RmlUi wants the next one:

```
folio invalidation: updated because 'mouse move', next update in never, until input
folio invalidation: updated because 'data model', next update in 0.694s
folio invalidation: updated because 'RmlUi asked', next update in 0.000s
```

- **`never, until input`**: the document is static. Correct for a settled menu.
- **`0.000s`**: something is animating, so every frame counts. Correct while a transition runs.
- **a small delay**: a timer, almost always a caret.
- **no lines at all after the first few**: the document is idle. If it should not be, something changed it without saying so.

RmlUi wakes a document for a running `animation` or `transition`, a blinking caret, a held scroll arrow and a smooth scroll. Folio adds input, loading a document, a resize, a data model change and a stylesheet reload.

## Layer cache

Each layer is fingerprinted from everything it draws plus its filters and mask. A layer whose fingerprint matches one from the last couple of frames reuses that picture.

- `folio.Layers.Cache 0` turns the cache off.
- `folio.Layers.CacheStats` logs how many layers were drawn and how many reused.

The rest of the layer and invalidation commands are in the [console reference](console.md).

## `uipreview` performance findings

| | |
|---|---|
| `W-PERF-RESIZINGLAYER` | A layer effect on an element whose box is transitioned. The expensive one, and the only one `--strict` fails on |
| `I-PERF-FULLSCREENLAYER` | A layer effect covering at least 75% of the viewport |
| `I-PERF-LAYERCOUNT` | How many elements draw one at all |
| `I-PERF-NEVERIDLES` | An animation that runs forever, so the document is drawn every frame. Layers whose content has not changed are reused from the cache; the rest are paid for every frame |

A vignette or a scrim built the obvious way is a legitimate document, and a texture is not automatically cheaper, so only the first is a warning.

## Font report

```
folio.Font.Report
```

Faces, variation tuples, page and glyph counts, cache hits and misses. Misses climbing during play means glyphs are being generated at runtime.

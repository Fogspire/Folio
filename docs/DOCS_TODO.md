# Docs to write

Notes-to-self about the documentation, not documentation. Everything else in `docs/` is written for
someone learning Folio: plain language, task-shaped, no internals unless they change what a reader
should do.

Reference material that already exists and should be drawn on, not duplicated:

- `UI_CONVENTIONS.md` — the capability reference. What works, what is banned, and why. Accurate but
  dense, and organised by CSS feature rather than by what a reader is trying to do.
- `NOTES.md` — build and engine-integration hazards. Almost all of it belongs nowhere near `docs/`.

---

## ~~Data binding, and whether to use MVVM~~

Written: `data_binding.md`, as a health bar walkthrough. Markup verified in the preview at dp 1,
including `format(0)`, division in expressions, percentage widths from `data-style-width`,
`data-class-*` switching, `&lt;` inside an attribute, and mock discovery from `ui/mock/<model>.json`.

Left for a later page: binding arrays (an inventory list), which reflection cannot do and which is
the first thing a reader will want after this.

---

## Other pages needed

- ~~**Getting started.**~~ Written: `hello_world.md`. Its markup and the `--strict` run were
  verified in the preview; the clean-project setup steps (2 and 3) are accurate to the scripts but
  have never been run on a project that did not already have Folio. Worth walking once.
- **Hosting a document** (`hosting.md` — both `hello_world.md` and `data_binding.md` link to it, so
  it is a dead link twice over until written). `UFolioWidget` in an existing widget tree, and the CommonUI focus-target
  override. The failure it prevents (gamepad silently doing nothing) is worth naming, since the
  symptom points nowhere near the cause.
- **Input and navigation.** Which keys navigate, the per-scheme switches, rebinding, and the
  confirm/back swap. `UI_CONVENTIONS.md` §8 has the material; it needs reordering around what a
  reader wants to achieve.
- **Text and fonts.** Variable weights, the font-effect size cap, and the small-text trade-off, which
  a reader should hear about before choosing a body size rather than after.
- **Vector art and sprites.** What to hand an artist, and the constraints that actually bite
  (single-colour silhouettes, no gradients yet, no embedded rasters, no ninepatch on the vector
  atlas). Gradients are on the backlog in `FOLIO_BRIEF.md`, so word this so it does not need
  rewriting when they land.
- **Testing a document.** `uipreview`, `--strict`, and the screenshot and metrics reports. Cheap to
  write and the thing most likely to be skipped.

## Things to check before publishing any of it

- Every code sample compiles or is copied from something that does. Samples in `UI_CONVENTIONS.md`
  were written alongside working code; new ones for `docs/` will not have been.
- No em dashes in code comments in samples, per the house rule.
- Nothing derived from `DO_NOT_REDISTRIBUTE.svg` or its baked fields appears in an example.

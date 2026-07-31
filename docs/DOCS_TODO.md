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
  Step 7 (cooking, staging and `-uidev`) *was* verified end to end against a packaged Development
  client: documents, stylesheets and faces load from the pak, and a loose file in the override
  directory wins over it.
- ~~**Hosting a document.**~~ Written: `hosting.md`. `UFolioWidget` in an existing widget tree, view
  model binding and why it must precede the load, and the CommonUI focus-target override with the
  symptom it prevents named. Written from the header contracts and `UI_CONVENTIONS.md` §8 rather than
  from a session in the editor, so the focus advice is accurate to the code but the walkthrough has
  not been followed by anyone.
- ~~**Localization.**~~ Written: `localization.md`. Every claim in it was run: the gather produces 35
  strings under `UI.Settings` in the same manifest as the C++ and asset text, `folio.Loc.Report` shows
  them resolving, a patched translation displays, and `--culture pseudo --strict` behaves as described
  including leaving bound values unaccented. The `Config/Localization/Game.ini` it tells readers to copy
  ships at `templates/Config/Localization/Game.ini`.
- **Input and navigation.** Which keys navigate, the per-scheme switches, rebinding, and the
  confirm/back swap. `UI_CONVENTIONS.md` §8 has the material; it needs reordering around what a
  reader wants to achieve.
- **Text and fonts.** Variable weights, the font-effect size cap, and the small-text trade-off, which
  a reader should hear about before choosing a body size rather than after.
- **Vector art and sprites.** What to hand an artist, and the constraints that actually bite
  (single-colour silhouettes, no gradients yet, no embedded rasters, no ninepatch on the vector
  atlas). Gradients are on the backlog in `FOLIO_BRIEF.md`, so word this so it does not need
  rewriting when they land.
- ~~**Performance and profiling.**~~ Written: `performance_profiling.md`. Covers `stat Folio`, the
  trace channel, the invalidation path with its trace and its escape hatch, and the ordered list of
  what actually makes a screen slow. The numbers quoted in it were measured, not estimated.
- **Testing a document.** `uipreview`, `--strict`, and the screenshot and metrics reports. Cheap to
  write and the thing most likely to be skipped. Mention `check_world_path.sh` for anyone touching
  the renderer rather than a document.
- ~~**Documents in the world.**~~ Written: `world_documents.md`. The three draw modes, the Surface
  setup against `/Folio/M_FolioSurface`, and the console commands. After-tonemap and in-scene were
  confirmed in PIE; the Surface walkthrough was followed start to finish by someone other than its
  author and worked. The input section was written from the bugs found doing that, so its
  troubleshooting list is real rather than imagined.

## Things to check before publishing any of it

- Every code sample compiles or is copied from something that does. Samples in `UI_CONVENTIONS.md`
  were written alongside working code; new ones for `docs/` will not have been.
- No em dashes in code comments in samples, per the house rule.
- Nothing derived from `DO_NOT_REDISTRIBUTE.svg` or its baked fields appears in an example.

# Docs to write

Notes-to-self about the documentation, not documentation. Everything else in `docs/` is written for
someone learning Folio: plain language, task-shaped, no internals unless they change what a reader
should do.

Reference material that already exists and should be drawn on, not duplicated:

- `UI_CONVENTIONS.md` — the capability reference. What works, what is banned, and why. Accurate but
  dense, and organised by CSS feature rather than by what a reader is trying to do.
- `NOTES.md` — build and engine-integration hazards. Almost all of it belongs nowhere near `docs/`.

---

## Data binding, and whether to use MVVM

Worth a page of its own, because the honest answer is not the one people expect.

Folio binds any `UObject`'s properties as a data model. A source implementing
`INotifyFieldValueChanged` — which `UMVVMViewModelBase` does — updates the document as its fields
change; one that does not is re-read once a frame instead. So the choice of source is per-screen and
costs nothing to change later: the call site is the same either way, and adding FieldNotify to a
source needs no change to the document.

**Folio does not depend on the ModelViewViewModel plugin, and MVVM is not required.** Explain why,
because it is the opposite of the UMG advice a reader will arrive with:

- In UMG, most of MVVM's value is the *editor-side* binding workflow — the bindings panel, compiled
  bindings, the View extension. Folio uses none of it. What `UMVVMViewModelBase` gives a Folio
  project is a UObject with FieldNotify and nothing else, so the trade is very different from UMG,
  where MVVM is close to a no-brainer.
- RmlUi's data model **is already a view-model layer**. `data-model` plus data expressions is the
  view/view-model split. A `UMVVMViewModelBase` behind it can end up a view model feeding a view
  model.

When a view model earns its place:

- Data that changes irregularly, where push beats polling.
- State worth unit testing without a screen.
- Data whose lifetime should not be tied to the document's.

When it does not:

- A static screen — credits, a settings page read once — gains nothing from notification.
- Per-frame HUD data, where notification can be *worse*: `Context::Update` runs once a frame, so a
  hundred broadcasts and one poll both collapse into a single layout. Polling is the natural cadence.

**The rule to lead with is not "use MVVM".** It is *a document never reaches into gameplay* — one
boundary object per screen, whatever its base class. That is the invariant worth enforcing; the base
class is an implementation detail.

Also cover, briefly: only properties marked `BlueprintReadOnly`/`BlueprintReadWrite` or
`EditAnywhere` are bound, because internal state is not a binding surface; and arrays, maps and sets
cannot be bound by reflection, so a game exposes those itself.

---

## Other pages needed

- ~~**Getting started.**~~ Written: `hello_world.md`. Its markup and the `--strict` run were
  verified in the preview; the clean-project setup steps (2 and 3) are accurate to the scripts but
  have never been run on a project that did not already have Folio. Worth walking once.
- **Hosting a document** (`hosting.md` — `hello_world.md` already links to it, so it is a dead link
  until written). `UFolioWidget` in an existing widget tree, and the CommonUI focus-target
  override. The failure it prevents (gamepad silently doing nothing) is worth naming, since the
  symptom points nowhere near the cause.
- **Input and navigation.** Which keys navigate, the per-scheme switches, rebinding, and the
  confirm/back swap. `UI_CONVENTIONS.md` §8 has the material; it needs reordering around what a
  reader wants to achieve.
- **Text and fonts.** Variable weights, the font-effect size cap, and the small-text trade-off, which
  a reader should hear about before choosing a body size rather than after.
- **Vector art and sprites.** What to hand an artist, and the constraints that actually bite
  (single-colour silhouettes, no ninepatch on the vector atlas).
- **Testing a document.** `uipreview`, `--strict`, and the screenshot and metrics reports. Cheap to
  write and the thing most likely to be skipped.

## Things to check before publishing any of it

- Every code sample compiles or is copied from something that does. Samples in `UI_CONVENTIONS.md`
  were written alongside working code; new ones for `docs/` will not have been.
- No em dashes in code comments in samples, per the house rule.
- Nothing derived from `DO_NOT_REDISTRIBUTE.svg` or its baked fields appears in an example.

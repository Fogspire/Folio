# Recipes

Complete documents to copy, not components to link.

A settings screen differs too much between games to parameterise, and most of the way it does not
differ at all. So these are working documents: copy one into your own `UI/screens/`, change the
strings and the ids, and delete what you do not need. Nothing links them at runtime.

| | |
|---|---|
| `main_menu.rml` | Title, one navigable stack, a build stamp |
| `pause.rml` | A card over a scrim, Resume first |
| `confirm.rml` | A destructive confirmation, with the safe action focused |
| `dialogue.rml` | A line that types itself out, with emphasis, a shared place name and a continue prompt |
| `nameplate.rml` | A marker document: a plate above a head, laid out at the component's Size |
| `subtitles.rml` | Close captions: a stack of spoken lines, newest bright, each on its own clock |
| `prompts.rml` | Every drawn glyph the library ships |
| `bindings.rml` | A rebinding list over `UFolioInputBindings` |
| `circle.rml` | Circles, rings, capsules - and why a bordered box is not one |

Each one is rendered under `--strict` before it is committed, so a copied recipe starts clean.

What they demonstrate is worth keeping even when everything visible changes:

- **One navigation axis per screen.** A `.btn-stack` navigates vertically and drops left and right,
  so a pad cannot get stuck against a neighbour that happens to sit beside the menu.
- **Something is focused on arrival.** A fresh document focuses nothing, and the first direction press
  then runs the document's own nav and finds the first tabbable element in document order - which for
  a dialog is the content behind it. `autofocus` is what makes a screen reachable on a controller.
- **The safe action is the focused one.** A player mashing confirm should cancel, not delete.
- **`data-loc` on every string**, so a screen can be translated before anyone asks for it.
- **No literal colours.** Everything comes from the theme, so a recipe follows `folio.Theme`
  like the rest of the library.

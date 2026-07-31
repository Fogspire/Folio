# Setting up VSCode

Highlighting for `.rml` and `.rcss`, and a live preview beside the document. Five minutes, and it is
where most of your authoring time will be spent.

You need `uipreview` built:

```bash
cd <YourProject>/Plugins/Folio
./scripts/build.sh
```

## 1. Install the extension

It has no build step and no dependencies — one JavaScript file — so installing it is copying a
directory. Either copy or link `Plugins/Folio/Tools/vscode-folio` into VSCode's extensions folder:

```bash
# Windows, from a shell with permission to make links. Copying the directory works just as well.
mklink /D "%USERPROFILE%\.vscode\extensions\folio-preview" "<YourProject>\Plugins\Folio\Tools\vscode-folio"

# Linux and macOS
ln -s <YourProject>/Plugins/Folio/Tools/vscode-folio ~/.vscode/extensions/folio-preview
```

A link is worth preferring: the extension then updates with the plugin rather than going stale.

Then **reload the window** — `Ctrl+Shift+P`, `Developer: Reload Window`. VSCode only scans that folder
at startup, so nothing happens until you do.

## 2. Open the project, not the plugin

File → Open Folder → **your project root**, the one holding `Content/` and `ui/`.

This is the one thing that has to be right. The extension finds `uipreview` under `Plugins/Folio/bin/`
relative to the workspace, and passes the workspace as `--root`, which is what makes `/ui/...` and
`/folio/...` links resolve. Open the plugin directory on its own and the preview cannot find either.

## 3. Preview a document

Open any `.rml` and click the **preview icon** in the editor's title bar, top right. `Ctrl+Shift+V`
and the **Folio: Open Preview** command do the same thing.

The button only appears on `.rml` files, so it is not in the way the rest of the time.

It re-renders whenever you save an `.rml` **or** an `.rcss`, because a document links several
stylesheets and a change to one matters as much as a change to the markup. It follows the active
editor, so switching documents previews the one you are looking at.

Warnings appear beside the render rather than instead of it — `--strict` is on by default, so a
missing sprite or a property that parses but does nothing is reported rather than quietly rendered
past. `ui/examples/button.rml` has one, if you want to see what that looks like.

## Zooming

`Ctrl` and the wheel, the toolbar buttons, or `Ctrl` with `+`, `-` and `0`.

**Zooming re-renders rather than magnifying.** It scales the dp ratio, which is what a browser's zoom
does: the viewport stays the size `folio.size` asks for and the content within it grows, so you see
less of the document at more detail. Text and vector art get *sharper* as they grow, because they are
distance fields - and that is the only way a preview can tell you whether the real thing is crisp.
Scaling a screenshot would show you the scaler's work instead.

A zoom is a render, so it takes as long as a save does.

`folio.size` is separate, and sets the viewport. It is not limited by your display: the preview
renders to an offscreen target, so `3840x2160` on a 1080p monitor produces a real 4K render rather
than a clamped one.

## Editing from the preview

The preview is not read-only.

**Hover** and the element under the cursor is outlined with a label naming it and its size, so you can
see what a click will take before you take it. Nested elements often share an edge, and without this
you are guessing.

**Click** to select. The bar above the render then names what you picked and lists every rule that
reaches it, with its file and line. Clicking one opens it beside the preview.

The rules are listed rather than jumped to, deliberately: an element is usually styled by several
rules and there is no way to know which one you meant. An earlier version jumped straight to the first
one and it felt broken, because the first property is whichever the enumeration reached first - the
destination looked arbitrary because it was.

**Drag a selected element** to move it. On release you are asked *which rule* to edit, because that is
a real choice and getting it wrong silently is worse than asking: moving one button by editing `.btn`
moves every button, and moving it by editing `#continue` moves only that one. Rules that already set
the property are offered first.

The edit is applied as **one property on one rule**, in place. Comments, blank lines and indentation
are untouched - the diff is one line, or one run of characters inside one line. It goes through the
editor's undo stack, so `Ctrl+Z` reverses it like anything else you typed.

**Put the caret in a rule** in an `.rcss` file and every element that rule reaches lights up in the
preview, in a different colour to a selection. It answers "what does this actually style" without
reading the markup.

### What it will not do

It edits properties on rules. It does not add rules, move elements between parents, or write markup -
documents stay hand-authored text that a tool adjusts. If nothing styles an element, the drag tells you
so rather than inventing a rule to hold the change.

## What the preview is worth trusting

This is the whole reason the tool exists, so it is worth being precise about.

**Layout matches the engine exactly.** Not within a tolerance — exactly.
`./scripts/compare_renderers.sh` renders every document through both renderers and compares where
each text line landed, and it passes across the documents the game ships. If a box is in the right
place here, it is in the right place in the game.

**The appearance of layer effects is not compared.** `box-shadow`, `filter`, `backdrop-filter` and
`mask-image` are drawn in OpenGL here and replayed through RHI in the engine. Layout under an effect
is still exact; a shadow's colour or blur radius is not something the preview can promise. A document
using one says so in the panel rather than leaving you to wonder.

**Colour will differ from a screenshot of the game, always.** The preview does no sRGB conversion
where Slate linearises vertex colour and applies display gamma. Judge colour in the engine, and treat
the preview as authoritative about everything else.

## Highlighting

`.rml` and `.rcss` highlight as soon as the extension is installed. Both reuse VSCode's built-in
grammars — XML for markup, CSS for stylesheets — so there is nothing to keep in step as those improve.
A `<style>` block inside a document is highlighted as RCSS rather than as markup.

**RCSS is registered as its own language rather than as CSS**, deliberately. VSCode's CSS validator is
bound to the `css` language and does not know `decorator`, `nav`, `font-effect`, `drag` or `dp` units,
so registering `.rcss` as CSS would put a red squiggle under most of a correct stylesheet. The cost is
that you get no CSS completions, which is the right way round: a completion list offering
`grid-template-columns` and `var()` would be actively misleading, since RCSS has neither.

## Settings

All under `folio.` in settings:

| | |
|---|---|
| `uipreviewPath` | Path to the binary. Empty searches the workspace. |
| `root` | Passed as `--root`. Empty uses the workspace folder. |
| `size` | Render size, default `1280x720`. |
| `dpRatio` | Pinned dp ratio, default `1`. **Set this to whatever your game uses**, or every length in the preview differs from the game's — which reads as a layout bug rather than a setting. |
| `strict` | Report warnings rather than rendering past them. On by default. |
| `culture` | `pseudo` inflates strings and accents them, which finds overflow before translation exists. |
| `themes` | Themes to activate, matching `folio.Theme` in the engine. |

## If nothing appears

- **"Could not find uipreview".** Either the workspace is not the project root (see step 2), or the
  tool has not been built. `./scripts/build.sh`.
- **The panel is empty and reports nothing.** The document failed to parse. Run it directly to see
  why: `./bin/win64/uipreview.exe <file> --root . --strict`.
- **Nothing happens on save.** The preview only follows `.rml` and `.rcss`. Saving a `.json` mock or a
  font will not re-render; use **Folio: Refresh Preview**.
- **The extension is not there after installing.** Reload the window. VSCode scans for extensions once
  at startup.

## Where to go next

- **[Hello world](hello_world.md)** — the project setup this assumes.
- **[What RCSS actually supports](../UI_CONVENTIONS.md)** — read it before authoring anything real.
  RmlUi is close enough to CSS to mislead, and the preview will not warn you about a property that
  parses and does nothing unless `--strict` is on.

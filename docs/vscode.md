# Setting up VSCode

Highlighting for `.rml` and `.rcss`, and a live preview beside the document.

**The preview is deprecated.** [Folio Studio](studio.md) does everything it does, including baking an element and the animation timeline, on Windows, macOS and Linux. The preview will be removed in a later release; the highlighting stays. The preview is Windows only, because `uipreview` is.

You need `uipreview` built:

```bash
cd <YourProject>/Plugins/Folio
./scripts/build.sh
```

## 1. Install the extension

It is one JavaScript file with no build step, so installing it is copying a directory. Copy or link `Plugins/Folio/Tools/vscode-folio` into VSCode's extensions folder:

```bash
# Windows, from a shell with permission to make links. Copying the directory works just as well.
mklink /D "%USERPROFILE%\.vscode\extensions\folio-preview" "<YourProject>\Plugins\Folio\Tools\vscode-folio"

# Linux and macOS
ln -s <YourProject>/Plugins/Folio/Tools/vscode-folio ~/.vscode/extensions/folio-preview
```

Prefer a link: the extension then updates with the plugin.

Then **reload the window**: `Ctrl+Shift+P`, `Developer: Reload Window`. VSCode only scans that folder at startup.

## 2. Open the project, not the plugin

File → Open Folder → **your project root**, the one holding `Content/` and `UI/`.

The extension finds `uipreview` under `Plugins/Folio/bin/` relative to the workspace, and passes the workspace as `--root`, which is what makes `/UI/...` and `/folio/...` links resolve. Open the plugin directory on its own and the preview finds neither.

## 3. Preview a document

Open any `.rml` and click the **preview icon** in the editor's title bar, top right. `Ctrl+Shift+V` and the **Folio: Open Preview** command do the same. The button only appears on `.rml` files.

The preview follows the active `.rml` editor. It re-renders whenever any `.rml`, `.rcss`, `.json`, `.png`, `.ttf` or `.otf` in the workspace changes, saved from this editor or written by anything else (a generator, a script or an agent), outside build folders such as `bin`, `Binaries` and `Intermediate`. Several files changing at once coalesce into one render.

Warnings appear beside the render rather than instead of it: `--strict` is on by default, so a missing sprite or a property that parses but does nothing is reported. `UI/examples/button.rml` has one.

## Open it in the standalone renderer

Beside the preview button is **Folio: Open in uipreview**, which opens the document in its own window rather than in a webview.

The preview is the same binary rendering to a PNG, which is wrong for anything that has to *run*: real keyboard and gamepad input, the RmlUi debugger on F8, an animation at its own frame rate rather than the sampled strip the [timeline](#the-timeline) plays. It takes the same `folio.root`, `folio.size`, `folio.strict`, `folio.culture` and `folio.themes` settings, watches the file itself, and outlives the editor closing.

## Zooming

`Ctrl` and the wheel, the toolbar buttons, or `Ctrl` with `+`, `-` and `0`.

**Drag to pan.** Middle mouse drags the render around; hold space and any drag does the same, which leaves left-drag free to pick elements.

**Zooming re-renders rather than magnifying.** It scales the dp ratio, as a browser's zoom does: the viewport stays the size `folio.size` asks for and the content within it grows. Text and vector art get *sharper* as they grow, because they are distance fields, which is how the preview tells you whether the real thing is crisp.

A zoom is a render, so it takes as long as a save does.

`folio.size` sets the viewport. It is not limited by your display: the preview renders offscreen, so `3840x2160` on a 1080p monitor produces a real 4K render rather than a clamped one.

## The timeline

A render shows what a document settles into, after an entry animation has finished. The **timeline** button above the preview (or **Folio: Toggle Timeline** in the title bar) renders a frame per step from the document's first update and puts a transport under the render.

**Play** runs it from the start, looping. **Drag the scrubber** to any instant. **Left and right arrows step one frame**. Space is the pan gesture, so play is the button rather than a key.

Two seconds at 30fps by default: `folio.timelineDuration` and `folio.timelineFps` change that. Both are simulated: the strip is rendered at a fixed step, so it is identical every time, and `folio.timelineFps` is the sample interval, not the game's frame rate.

**It costs a render per frame.** A still is a fraction of a second and a strip is a few, on every save while it is open, so it is off by default; close it when you go back to layout.

**Elements can only be picked at the end of the strip.** The rules that reach an element are reported for one instant, and mid-animation the elements are elsewhere. Scrub back to the end and hovering, selecting and dragging come back.

The standalone renderer is the better tool for anything that has to *run*: real input, the debugger, an animation at the real frame rate.

## Editing from the preview

The preview is not read-only.

**Hover** and the element under the cursor is outlined with a label naming it and its size, so you see what a click will take. Nested elements often share an edge.

**Click** to select. The bar above the render then names what you picked and lists every rule that reaches it, with its file and line. Clicking one opens it beside the preview.

**Click past everything, or press `Escape`, to deselect.**

**Alt-click steps outwards** through whatever else is under the same point. A modal's scrim covers the whole screen, and this is how to select anything it sits over. Anything covering most of the document is outlined without a tint, so it never hides what is underneath.

The rules are listed rather than jumped to, because an element is usually styled by several rules and there is no way to know which one you meant.

**Drag a selected element** to move it. On release you are asked *which rule* to edit: moving one button by editing `.btn` moves every button, and moving it by editing `#continue` moves only that one. Rules that already set the property are offered first.

The edit is applied as **one property on one rule**, in place. Comments, blank lines and indentation are untouched: the diff is one line, or one run of characters inside one line. `Ctrl+Z` reverses it.

**Put the caret in a rule** in an `.rcss` file and every element that rule reaches lights up in the preview, in a different colour to a selection. It answers "what does this style" without reading the markup.

### What it will not do

It edits properties on rules. It does not add rules, move elements between parents, or write markup. If nothing styles an element, the drag tells you so rather than inventing a rule to hold the change.

## What the preview is worth trusting

**Layout matches the engine exactly**, not within a tolerance. `./scripts/compare_renderers.sh` renders every document through both renderers and compares where each text line landed, and it passes across the documents the game ships.

**The appearance of layer effects is not compared.** `box-shadow`, `filter`, `backdrop-filter` and `mask-image` are drawn in OpenGL here and replayed through RHI in the engine. Layout under an effect is still exact; a shadow's colour or blur radius is not something the preview can promise. A document using one says so in the panel.

**Colour always differs from a screenshot of the game.** The preview does no sRGB conversion where Slate linearises vertex colour and applies display gamma. Judge colour in the engine, and treat the preview as authoritative about everything else.

## Highlighting

`.rml` and `.rcss` highlight as soon as the extension is installed. Both reuse VSCode's built-in grammars (XML for markup, CSS for stylesheets). A `<style>` block inside a document is highlighted as RCSS rather than as markup.

**RCSS is registered as its own language rather than as CSS.** VSCode's CSS validator is bound to the `css` language and does not know `decorator`, `nav`, `font-effect`, `drag` or `dp` units, so registering `.rcss` as CSS would put a red squiggle under most of a correct stylesheet. The cost is no CSS completions, which would offer `grid-template-columns` and `var()`, and RCSS has neither.

## Settings

All under `folio.` in settings:

| | |
|---|---|
| `uipreviewPath` | Path to the binary. Empty searches the workspace. |
| `root` | Passed as `--root`. Empty uses the workspace folder. |
| `size` | Render size, default `1280x720`. |
| `dpRatio` | Pinned dp ratio, default `1`. **Set this to whatever your game uses**, or every length differs from the game's, which reads as a layout bug. |
| `timelineDuration` | Seconds of simulated time the timeline covers, default `2`. |
| `timelineFps` | Frames per second it samples at, default `30`. Longer or finer costs proportionally more. |
| `strict` | Report warnings rather than rendering past them. On by default. |
| `culture` | `pseudo` inflates strings and accents them, which finds overflow before translation exists. |
| `themes` | Themes to activate, matching `folio.Theme` in the engine. |

## If nothing appears

- **"Could not find uipreview".** Either the workspace is not the project root (see step 2), or the tool has not been built. `./scripts/build.sh`.
- **The panel is empty and reports nothing.** The document failed to parse. Run it directly to see why: `./bin/win64/uipreview.exe <file> --root . --strict`.
- **Nothing happens on save.** The preview follows every `.rml`, `.rcss`, `.json` and font in the workspace. It cannot see a file outside the workspace; use **Folio: Refresh Preview**.
- **The extension is not there after installing.** Reload the window. VSCode scans for extensions once at startup.

## Where to go next

- **[Hello world](hello_world.md)**: the project setup this assumes.
- **[What RCSS actually supports](../UI_CONVENTIONS.md)**: read it before authoring anything real. RmlUi is close enough to CSS to mislead, and the preview does not warn you about a property that parses and does nothing unless `--strict` is on.

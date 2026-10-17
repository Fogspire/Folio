# Folio Studio

A visual editor for Folio documents. The canvas is the real renderer, the same code `uipreview` and the game draw with, so what you see is what ships. Runs on Windows, macOS and Linux.

## Get it working

You need [Node](https://nodejs.org) 20 or newer and a checkout of Folio. Nothing else: no compiler, no Unreal.

```bash
cd <YourProject>/Plugins/Folio
./scripts/studio.sh
```

The first run installs dependencies and takes a minute. After that it starts in seconds. On Windows run it from Git Bash.

To start it without a terminal from then on, choose **File > Create Shortcuts**, or run `./scripts/studio.sh shortcut`. Either adds Folio Studio to the desktop and Start menu on Windows, the applications menu on Linux, and `~/Applications` on macOS. The shortcut opens the last build, so after a `git pull` run `./scripts/studio.sh build`.

Run from a plugin inside a project, it opens that project. To open another, pass the folder that contains `UI/` (`./scripts/studio.sh run <folder>`) or use **File > Open Project**. Studio opens the first document it finds:

- **Files** lists the project's documents. Click an `.rml` to put it on the canvas, or an image or font to look at it. Right-click a folder for a new document, a new document from a recipe, or a new stylesheet, and a file to delete it.
- **Ctrl+T** goes to a file by name. Type any part of it, such as `alti` for `altitude.rml`, pick from the list with Up and Down, and press Enter. Tab completes the chosen name.
- Every document you open stays open as a tab over the canvas and in **Code**. Click a tab to bring that document to the canvas. Middle-click closes a tab, and right-click closes the others or those to one side.
- **Click** an element to select it. **Alt-click** steps out to its parent. **Double-click** text to edit it in place.
- **Drag** a positioned element to move it, or any other element to reorder it among its siblings. Drag the handles to resize. A size set by a rule changes in that rule, so every element sharing it resizes; hold Shift to resize only this one. **File > Preferences** turns that off.
- **Properties** edits styles, classes, attributes and text. Each value shows the rule it comes from; click the rule to open it in **Code**.
- **Library** has elements to drag onto the canvas, and recipes to start a new screen from. Click a recipe to preview it, then **Use** to copy it into `UI/screens/` and open the copy.
- **Right-click** an element on the canvas or in **Outline** for cut, copy, paste, duplicate, delete and wrap.
- **Alt+Up** selects the parent of the selected element, **Alt+Down** its first child, and **Alt+Left** and **Alt+Right** its siblings.
- **Ctrl+wheel** zooms the canvas, and **middle-drag** pans it. **Ctrl+0** fits it back in view. **Ctrl+Z** undoes the last edit from anywhere outside a text editor.
- **Timeline** (Ctrl+Shift+T) holds the document's clock. Play it from the first frame, or drag to any moment, to see an entry animation that has otherwise finished before you look.
- **Right-click > Bake to PNG** renders an element alone to an image, so a costly effect or a slanted edge can ship as a texture. Samples draws it at two or four times the pixels and averages them down, which smooths an angled edge at the same size. Background puts it over white, black, 50% grey or a colour you pick instead of leaving it transparent. Right-click an `.rml` in Files to bake the whole document, or a folder to bake every document in it to a PNG of the same name, into that folder or one you choose. A layer effect (`box-shadow`, `filter`, `backdrop-filter`, `mask-image`) is refused, because the engine draws those differently; bake those in the editor with `folio.Bake`.
- **Revert** in the top bar, File > Revert Unsaved Changes, or a tab's right-click menu puts unsaved files back as they are on disk. Undo brings the changes back.
- **Terminal** is a shell in the project's folder, for git or anything else you run from a command line. Copy is Ctrl+Shift+C, paste is Ctrl+V.
- The bar along the bottom explains whatever is under the pointer. **Messages** there, or **View > Messages**, lists every message the top bar has shown this session; the top bar clears its message on your next click or key, or after a few seconds.
- **Ctrl+S** saves. Until then your edits are previewed but not on disk, so the running game does not see them.

Every edit is a change to the `.rml` or `.rcss` text, and nothing else in the file moves. You can keep editing by hand in **Code**, in VSCode, or anywhere else; Studio picks up changes from disk.

If the game is running in the editor, saving in Studio hot-reloads the document there, exactly as saving from any other editor does.

## Troubleshoot

| What you see | What to do |
|---|---|
| `Node is not installed` | Install Node 20 or newer, open a new terminal, run it again. |
| `the renderer is missing from Tools/studio/engine/dist` | Your checkout is incomplete. `git status` should show nothing missing under `Tools/studio/engine/dist`; `git checkout -- Tools/studio/engine/dist` restores it. |
| "This document did not load" | Open **Problems**. It lists what the renderer rejected, with file and line. |
| The canvas shows the document with no data | The document binds a model with no mock. Open **Data** and create one. |
| A screen costs frames while it sits still | Open **Activity**. It says whether the document idles in the game, and names every animation that runs forever (which stops it idling), what can wake it (transitions, animations that end, data bindings), and every element drawing a layer. Click a row to select the element. |
| "This element is not written in the document as it appears here" | The element comes from a template or was generated at run time. Select its parent, or edit the template's own file. |
| A panel is gone | **View** lists every panel and reopens a closed one. **View > Layout > Reset Layout**, or F10, puts them all back. |
| Terminal says it could not start | Run `./scripts/studio.sh` again so it reinstalls dependencies. If you set a shell in **File > Preferences**, check its path. |
| A key does nothing, or does the wrong thing | **File > Keyboard Shortcuts** shows what each key runs. Restore All Defaults there undoes every change. |
| A style change lands in a rule you did not expect | **Properties** shows where each value is written and "New properties go to" says where a new one will be. Change the target there. |
| An edit to a property seems ignored | The value is set by a rule Folio ships, shown in italics. Studio never edits those; it writes an override to your own rule instead. Check the rule order in your stylesheet. |
| A blur that reaches the edge of the screen looks slightly different from the game | A known difference in Studio's renderer. See [known gaps](known_gaps.md). Everything else matches `uipreview` to within two colour levels. |
| macOS says the packaged app cannot be opened | It is unsigned. Right-click it, choose Open, and confirm once. `studio.sh run` has no such step. |
| "Folio's Unreal libraries need rebuilding" or "uipreview needs rebuilding" | A pull changed what they are built from. Click **Rebuild**: it runs the build script in **Terminal**, and the notice clears when it succeeds. It needs Git Bash on Windows and the same tools as running `scripts/build_ue_libs.sh` or `scripts/build.sh` yourself. Close the Unreal editor before rebuilding the libraries. |
| No update notice ever appears | Releases are found by tag. Run `git ls-remote --tags origin` in the checkout: if that fails or lists nothing like `v1.2.3`, there is nothing to find. |

To see what the renderer printed while loading a document, start Studio from a terminal: its diagnostics go to **Problems**, and anything else to that terminal.

## Tune it

### Canvas

| Control | What it does |
|---|---|
| Size | The viewport the document is laid out in. A document with `<meta name="folio-size" content="960x640"/>` opens at that size, at Scale 1×. |
| Scale | The dp ratio. Auto follows the viewport height against 1080, as the game does. |
| Zoom | How large the canvas is drawn. Zoomed in, the document is drawn with more pixels, as the game draws it on a larger screen, so text stays sharp. Lengths in `dp` and `rem` scale with it; lengths in `px` keep their size, which can move a layout that relies on them. Ctrl+wheel, Ctrl+= and Ctrl+- zoom, Ctrl+0 fits, Ctrl+1 is actual size. |
| Edit / Play | Edit selects and changes elements. Play sends the mouse and keyboard to the document, so hover, focus, navigation and `data-event-*` behave as in game. |

### Layout

Studio reopens with the panels as you left them. **View > Layout > Save Layout As** writes the arrangement to a file, and layouts saved in the folder it offers are listed in the same menu. **Load Layout** opens one from anywhere.

### Preferences

**File > Preferences** sets the terminal's shell and arguments, the font size of the terminal and of **Code**, and whether Studio restores the last layout and checks for updates when it starts.

With no shell set, the terminal runs Git Bash on Windows when it is installed and PowerShell otherwise, and `$SHELL` on macOS and Linux. The `FOLIO_STUDIO_SHELL` environment variable is used before those defaults.

### Keyboard shortcuts

**File > Keyboard Shortcuts** lists every command. Change gives one a new key, taking it from whatever had it; Remove leaves it with none; Default puts it back.

Ctrl+Shift+click an item in the menu bar to set or remove that item's key without opening the list.

### Where a style edit is written

- A property that one of your rules already sets is changed in that rule.
- A property nothing of yours sets yet goes to the target in **New properties go to**: one of the element's own rules, a new rule for its id or class in the document's `<style>` block, or its `style` attribute.
- Rules under `/folio/` are never edited.

### Data and bindings

**Data** lists each model the document binds, from `UI/mock/<model>.json`.

Create a schema to type a model. It is saved as `UI/schema/<model>.json` and the mock is regenerated from it on every change, so the two cannot drift. With a schema, Studio flags bindings the model does not have and completes binding names as you type them in **Properties**.

To generate the C++ base class from a schema:

```bash
./scripts/viewmodel.sh --schema UI/schema/pause.json \
    --out Source/MyGame/UI/Generated/PauseViewModelBase.h --api MYGAME_API
```

Derive your view model from the generated class and rebuild. See [generating a view model](view_models.md).

To go the other way, from classes you already have: play the screens once in the Unreal editor, then choose **Folio > Export View Model Schemas** in the toolbar. That writes a schema for every class that was bound, and needs no rebuild.

### Updates

Studio checks the checkout's `origin` for a release tag newer than its own version when it starts, and says so in the top bar. **Help > Check for Updates** checks again, and **Help > Check for Updates at Startup** turns the check off or on. To update:

```bash
git pull
./scripts/studio.sh run
```

### A copy that runs without the checkout

```bash
./scripts/studio.sh package
```

Writes a runnable app for the machine you are on to `Tools/studio/release`.

### Changing the renderer

Only needed after changing C++ under `Tools/uipreview`, `Tools/studio/engine` or `Source/FolioCore`:

```bash
./scripts/studio.sh engine     # rebuilds the WebAssembly renderer; fetches Emscripten on first use
./scripts/studio.sh parity     # compares its output with native uipreview
./scripts/studio.sh test
```

Commit `Tools/studio/engine/dist` afterwards. `scripts/verify.sh` fails while that build is older than its sources.

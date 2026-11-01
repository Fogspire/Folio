# Folio Studio

A visual editor for Folio documents on Windows, macOS and Linux, drawing with the same renderer `uipreview` and the game use, and writing every edit back as text.

![Folio Studio with a document on the canvas](https://github.com/user-attachments/assets/c4e83c80-b060-46f5-be5e-6ad856ce07c7)

## Get it working

You need [Node](https://nodejs.org) 20 or newer and a checkout of Folio. No compiler, no Unreal.

```bash
cd <YourProject>/Plugins/Folio
./scripts/studio.sh
```

The first run installs dependencies and takes a minute. On Windows run it from Git Bash.

Run from a plugin inside a project, it opens that project and the first document it finds. To open another, pass the folder that contains `UI/` (`./scripts/studio.sh run <folder>`) or use **File > Open Project**.

Then:

1. Click an `.rml` in **Files** to put it on the canvas. **Ctrl+T** goes to a file by name.
2. **Click** an element to select it, **drag** it to move it, drag its handles to resize it, and **double-click** text to edit it in place.
3. Change styles, classes, attributes and text in **Properties**, or edit the source in **Code**.
4. **Ctrl+S** saves. Until then edits are not on disk, so the running game does not see them. If the game is running in the editor, saving hot-reloads the document there.

Every edit is a change to the `.rml` or `.rcss` text, and nothing else in the file moves. Edit by hand in **Code**, VSCode or anywhere else; Studio picks up changes from disk.

To start it without a terminal, choose **File > Create Shortcuts**, or run `./scripts/studio.sh shortcut`. Either adds Folio Studio to the desktop and Start menu on Windows, the applications menu on Linux, and `~/Applications` on macOS. The shortcut opens the last build, so after a `git pull` run `./scripts/studio.sh build`.

Every panel, mouse action and shortcut is in the [Folio Studio reference](studio_reference.md).

## Show a document's data

**Data** lists each model the document binds, from `UI/mock/<model>.json`. A document that binds a model with no mock shows no data until you create one there.

Create a schema to type a model. It is saved as `UI/schema/<model>.json` and the mock is regenerated from it on every change. With a schema, Studio flags bindings the model does not have and completes binding names as you type them in **Properties**.

To generate the C++ base class from a schema:

```bash
./scripts/viewmodel.sh --schema UI/schema/pause.json \
    --out Source/MyGame/UI/Generated/PauseViewModelBase.h --api MYGAME_API
```

Derive your view model from the generated class and rebuild. See [generating a view model](view_models.md).

From classes you already have: play the screens once in the Unreal editor, then choose **Folio > Export View Model Schemas** in the toolbar. That writes a schema for every class that was bound, and needs no rebuild.

## If something is wrong

| What you see | What to do |
|---|---|
| `Node is not installed` | Install Node 20 or newer, open a new terminal, run it again. |
| `the renderer is missing from Tools/studio/engine/dist` | Your checkout is incomplete. `git status` should show nothing missing under `Tools/studio/engine/dist`; `git checkout -- Tools/studio/engine/dist` restores it. |
| "This document did not load" | Open **Problems**. It lists what the renderer rejected, with file and line. |
| The canvas shows the document with no data | The document binds a model with no mock. Open **Data** and create one. |
| A screen costs frames while it sits still | Open **Activity**. It says whether the document idles in the game, and names every animation that runs forever (which stops it idling), what can wake it (transitions, animations that end, data bindings), and every element drawing a layer. Click a row to select the element. |
| A binding completes no field names | The model has neither `UI/schema/<model>.json` nor `UI/mock/<model>.json`, or the one it has is not valid JSON. Open **Data**. |
| A property that works is underlined as unknown | The vocabulary is older than the renderer. Run `./scripts/studio.sh engine`, which regenerates both. |
| "This element is not written in the document as it appears here" | The element comes from a template or was generated at run time. Select its parent, or edit the template's own file. |
| A panel is gone | **View** lists every panel and reopens a closed one. **View > Layout > Reset Layout**, or F10, puts them all back. |
| Terminal says it could not start | Run `./scripts/studio.sh` again to reinstall dependencies. If you set a shell in **Edit > Preferences**, check its path. |
| A key does nothing, or does the wrong thing | **Edit > Keyboard Shortcuts** shows what each key runs. Restore All Defaults there undoes every change. |
| A style change lands in a rule you did not expect | **Properties** shows where each value is written and "New properties go to" says where a new one goes. Change the target there. See [where a style edit is written](studio_reference.md#where-a-style-edit-is-written). |
| An edit to a property seems ignored | The value is set by a rule Folio ships, shown in italics. Studio writes an override to your own rule instead. Check the rule order in your stylesheet. |
| A resize changed other elements too | The size was set by a rule, so every element sharing that rule resized. Hold Shift to resize only this one, or turn this off in **Edit > Preferences**. |
| Bake to PNG refuses an element | It draws a layer effect (`box-shadow`, `filter`, `backdrop-filter`, `mask-image`), which the engine draws differently. Bake it in the editor with `folio.Bake`; see [performance and profiling](performance_profiling.md#bake-a-layer-effect). |
| A blur that reaches the edge of the screen looks slightly different from the game | A known difference. See [known gaps](known_gaps.md). Everything else matches `uipreview` to within two colour levels. |
| macOS says the packaged app cannot be opened | It is unsigned. Right-click it, choose Open, and confirm once. `studio.sh run` has no such step. |
| "Folio's Unreal libraries need rebuilding" or "uipreview needs rebuilding" | A pull changed what they are built from. Click **Rebuild**: it runs the build script in **Terminal**, and the notice clears when it succeeds. It needs Git Bash on Windows and the same tools as running `scripts/build_ue_libs.sh` or `scripts/build.sh` yourself. Close the Unreal editor before rebuilding the libraries. |
| No update notice ever appears | Releases are found by tag. Run `git ls-remote --tags origin` in the checkout: if that fails or lists nothing like `v1.2.3`, there is nothing to find. |

To see what the renderer printed while loading a document, start Studio from a terminal: its diagnostics go to **Problems**, and anything else to that terminal.

## Update

At startup Studio checks the checkout's `origin` for a newer release tag and says so in the top bar. **Help > Check for Updates** checks again, and **Help > Check for Updates at Startup** turns the check off or on. To update:

```bash
git pull
./scripts/studio.sh run
```

## A copy that runs without the checkout

```bash
./scripts/studio.sh package
```

Writes a runnable app for the machine you are on to `Tools/studio/release`.

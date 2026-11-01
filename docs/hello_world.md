# Hello world

Getting Folio into a project and a document on screen. About fifteen minutes, mostly the first build.

You need an Unreal 5.8 project that already builds, and:

- **Windows:** Visual Studio 2022 with the C++ desktop workload. Folio finds the CMake and Ninja that ship with it.
- **macOS:** Xcode, plus CMake and Ninja (`brew install cmake ninja`).
- **Linux:** the engine's own toolchain, which the engine's `Setup.sh` installs, plus CMake and Ninja.

macOS and Linux are beta; see [platform support](../README.md#platform-support).

## 1. Put the plugin in the project

Copy or clone Folio to:

```
<YourProject>/Plugins/Folio/
```

## 2. Build the libraries Folio needs

Folio compiles RmlUi and msdfgen from source against your engine's toolchain. This is a one-off:

```bash
cd <YourProject>\Plugins\Folio
./scripts/build_ue_libs.sh
```

A cold run takes a few minutes and clones dependencies. Skip it and the project fails to build with a message telling you to run it.

The engine comes from your `.uproject`'s `EngineAssociation`. If that cannot be resolved, pass it:

```bash
./scripts/build_ue_libs.sh --engine-dir "Z:/Epic Games/Src/5.8/Engine"
```

**The editor build fails with unresolved `__std_rotate`, `__std_find_not_ch_1` or similar from `rmlui.lib`.** The libraries were built with a different MSVC toolset than Unreal's. The script uses the toolset UnrealBuildTool picks by default and does not read `CompilerVersion` from `BuildConfiguration.xml`. Rerun it with `--toolset <version>`, using the `MSVC\<version>` folder named on the `Using Visual Studio ... toolchain` line in UBT's log.

## 3. Enable the plugin

Add Folio to your `.uproject`:

```json
{
	"Name": "Folio",
	"Enabled": true
}
```

Then build your editor target. Folio has two modules and no content, so nothing else needs setting up.

## 4. Write a document

Documents live in a **`UI/` folder at the project root**: beside `Content/`, not inside it. They are plain text files and belong in source control with your code.

Create `<YourProject>/UI/hello.rml`:

```html
<rml>
<head>
	<title>Hello</title>
	<link type="text/rcss" href="/folio/folio.rcss"/>
	<style>
		body {
			background-color: #1f1f28;
			color: #e8e8f0;
			font-size: 24dp;
			padding: 32dp;

			/* A document is content-sized unless told otherwise. */
			width: 100%;
			height: 100%;
			box-sizing: border-box;
		}
	</style>
</head>
<body>
	<p>Hello world</p>
</body>
</rml>
```

- **`/folio/folio.rcss`** is Folio's base stylesheet, resolved inside the plugin wherever it lives. Link it from every document: it sets the defaults the rest of the system assumes.
- **`dp`, not `px`.** A dp scales with the player's UI scale and resolution the way Unreal's UI does. Use `px` only when you mean one screen pixel.
- **You have not chosen a font.** Folio falls back to the engine's Roboto. Drop `.ttf` files in `<YourProject>/UI/fonts/` for your own; the first one found becomes the default.

## 5. Put it on screen

Start the editor and type in the console:

```
folio.ShowWindow UI/hello.rml
```

The document opens in its own window. It needs no PIE session and no game code, so it works even when something else stops the game from booting. `folio.CloseWindow` closes it.

To see it over a running game, start PIE and use:

```
folio.Show UI/hello.rml
```

`folio.Hide` removes it. Neither command takes keyboard focus: focus is the game's decision, and [hosting a document](hosting.md) covers it along with `UFolioWidget`, which is how a screen ships.

## 6. Iterate without the editor

**[Folio Studio](studio.md)** draws the document with Folio's own renderer and edits it visually or as text, on any desktop with no Unreal running. Run `./scripts/studio.sh` from the plugin.

The same renderer is a command-line tool, `uipreview`, which checks a document and takes screenshots of it in a build or for an agent. Build it once:

```bash
./scripts/build.sh
```

Then:

```bash
./bin/win64/uipreview.exe <YourProject>/UI/hello.rml --root <YourProject>
```

`--root` is the path to the project directory, not its name; every asset the document references resolves beneath it. From inside the plugin:

```bash
./bin/win64/uipreview.exe ../../UI/hello.rml --root ../..
```

A root that does not exist reports the document as unopenable, so check it first when a file you can see does not load.

It watches the file and reloads on save: no compiling and no editor restart.

If you write markup in VSCode, **[its extension](vscode.md)** adds highlighting for `.rml` and `.rcss`.

Two flags to know on day one:

```bash
# Treat warnings as errors and run the markup lint checks.
uipreview.exe ui\hello.rml --root . --strict

# Render once to a PNG and exit, opening no window.
uipreview.exe ui\hello.rml --root . --screenshot hello.png
```

Run with `--strict` before you commit a document. It catches unreachable focus, missing sprites and properties that parse but do nothing.

## 7. Make it survive packaging

Documents, stylesheets, fonts and images are **files, not assets**. The cooker knows nothing about them, so unless you stage them every screen comes up empty in a build that worked in the editor.

The **Folio** button in the level editor toolbar has **Set Up Packaging**, which writes every line below to `DefaultGame.ini` and leaves anything already there alone. **Stage Documents Used In This Level** covers the rest: it finds the directories the open level's documents live in, such as `/folio/examples/`, and stages those too.

What the menu item writes, if you would rather write it yourself:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
; Folio's distance field material is loaded by path, so nothing references it and the cooker never
; sees it. Without this, all text renders with no shader.
+DirectoriesToAlwaysCook=(Path="/Folio")
+DirectoriesToAlwaysStageAsUFS=(Path="../UI")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/rcss")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/fonts")
```

The `../` on the staging lines is intentional: these paths resolve against `Content`, not the project root, and the staged path collapses back to the project root, where Folio looks at runtime. Drop the `fonts` line if you ship your own faces in `UI/fonts`.

List your own subdirectories rather than `UI` wholesale if the tree holds things a player should not receive. **You cannot exclude a subdirectory of one you have staged**, which is why Folio's demo documents live in `Plugins/Folio/examples` rather than under `rcss`.

**Anything pointed at `/folio/examples/` is missing from a packaged build.** It works in the editor and in a standalone run, which read loose files, and renders nothing once packaged. Add this line while prototyping against them, and remove it before you ship:

```ini
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/examples")
```

To check, look for your files in the staging manifest rather than launching the build:

```powershell
Select-String -Path "$env:LOCALAPPDATA\..\..\<engine>\Engine\Programs\AutomationTool\Saved\Logs\FinalCopyWin64_UFSFiles.txt" -Pattern "/UI/"
```

### Changing UI on a device without cooking again

Launch a Development or Test build with `-uidev` and Folio reads from a `uidev` directory in the project (in a staged build that is `<Staged>/<YourProject>/uidev`, beside the `UI` tree) before it reads the package. `-uidev=<path>` points somewhere else. The directory mirrors the project, so `uidev/UI/screens/hud.rml` overrides `UI/screens/hud.rml`, and anything absent falls through to the package: a directory holding one document is enough. Use it to iterate UI on a console or a Steam Deck. It does not exist in Shipping.

## Where to go next

- **[What RCSS supports](../UI_CONVENTIONS.md).** What works, what is silently ignored, and what is banned. Read it before authoring anything real; RmlUi is close enough to CSS to mislead.
- **[Hosting a document](hosting.md)**: `UFolioWidget`, focus, and fitting into an existing widget tree.
- **[Data binding](data_binding.md)**: showing values from your game, and whether a view model is worth it.
- **[Generating a view model](view_models.md)**: the C++ behind a document's bindings, generated from the document and its mock.
- **[Setting up VSCode](vscode.md)**: highlighting and a live preview beside the document, and what the preview is and is not worth trusting.
- **[Documents in the world](world_documents.md)**: a document on a surface in the level, sharp at any distance.
- **[Localization](localization.md)**: keying your text, and getting it into Unreal's gather.
- **[Performance and profiling](performance_profiling.md)**: `stat Folio`, Insights, and why a settled document costs nothing.

## If nothing appears

- **No text at all, everything else fine.** No font registered. Check the log for `no font faces registered`.
- **`{{ braces }}` rendered literally.** The document declares `data-model` but nothing is bound. Add `UI/mock/<model>.json` for a design-time stand-in.
- **The window opens empty.** Check the path: `folio.ShowWindow` resolves relative to the project directory, so `UI/hello.rml`, not `Content/UI/hello.rml` and not an absolute path.
- **Fine in the editor, empty in a packaged build.** The files were not staged. See step 7.
- **An edit is ignored in a packaged build.** A stale copy in the `-uidev` override directory. Folio logs a warning naming the directory at startup.
- **Anything surprising.** [Internals](internals.md) is a ledger of things that have already caught someone out.

# Hello world

Getting Folio into a project and a document on screen. About fifteen minutes, most of it waiting on
the first build.

You need **Visual Studio 2022** with the C++ desktop workload, and an Unreal 5.8 project that
already builds. Nothing else has to be installed — Folio finds the CMake and Ninja that ship with
Visual Studio.

## 1. Put the plugin in the project

Copy or clone Folio to:

```
<YourProject>/Plugins/Folio/
```

## 2. Build the libraries Folio needs

Folio uses RmlUi and msdfgen, which are compiled from source against your engine's toolchain rather
than shipped as binaries. This is a one-off:

```bash
cd <YourProject>\Plugins\Folio
./scripts/build_ue_libs.sh
```

A cold run takes a few minutes and clones dependencies as it goes. If you skip this step the project
fails to build with a message telling you to come back and do it, so it is hard to get wrong.

The engine comes from your `.uproject`'s `EngineAssociation`. If that cannot be resolved, point at
the engine yourself:

```bash
./scripts/build_ue_libs.sh --engine-dir "Z:/Epic Games/Src/5.8/Engine"
```

## 3. Enable the plugin

Add Folio to your `.uproject`:

```json
{
	"Name": "Folio",
	"Enabled": true
}
```

Then build your editor target as usual. Folio has two modules and no content, so there is nothing
else to set up.

## 4. Write a document

Documents live in a **`UI/` folder at the project root** — beside `Content/`, not inside it. They
are plain text files, so they belong in source control with your code rather than as assets.

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

Three things worth noticing:

- **`/folio/folio.rcss`** is Folio's base stylesheet, resolved inside the plugin wherever it lives.
  Link it from every document: it sets the sane defaults the rest of the system assumes.
- **`dp`, not `px`.** A dp scales with the player's UI scale and resolution the way Unreal's own UI
  does. Reach for `px` only when you genuinely mean one screen pixel.
- **You have not chosen a font.** Folio falls back to the engine's Roboto, which is why this renders
  at all. Drop `.ttf` files in `<YourProject>/UI/fonts/` when you want your own; the first one found
  becomes the default.

## 5. Put it on screen

Start the editor and type in the console:

```
folio.ShowWindow UI/hello.rml
```

That opens the document in its own window. It needs no PIE session and no game code, which makes it
the quickest way to look at something — and the only way if something else in the project is
stopping the game from booting. `folio.CloseWindow` closes it.

To see it over a running game instead, start PIE and use:

```
folio.Show UI/hello.rml
```

`folio.Hide` removes it. Neither command takes keyboard focus, so nothing you type goes to the
document. That is deliberate: focus is a decision a game makes, and
[hosting a document](hosting.md) covers it along with `UFolioWidget`, which is how a screen ships.

## 6. Iterate without the editor

There is a standalone renderer that draws the same markup with no Unreal involved. Build it once:

```bash
./scripts/build.sh
```

Then:

```bash
./bin/win64/uipreview.exe <YourProject>/UI/hello.rml --root <YourProject>
```

`--root` is the path to the project directory, not its name, and every asset the document references
is resolved beneath it. Running from inside the plugin that is:

```bash
./bin/win64/uipreview.exe ../../UI/hello.rml --root ../..
```

A root that does not exist reports the document as unopenable rather than complaining about the root
itself, so check it first when a file you can see will not load.

It watches the file and reloads on save, so the loop is edit, save, look — no compiling and no
editor restart. This is where you will spend most of your authoring time.

**[Setting up VSCode](vscode.md)** puts that loop beside the document instead of in a second window,
and adds highlighting for `.rml` and `.rcss`. Worth doing before you write anything real.

Two flags worth knowing on day one:

```bash
# Treat warnings as errors and run the markup lint checks.
uipreview.exe ui\hello.rml --root . --strict

# Render once to a PNG and exit, opening no window.
uipreview.exe ui\hello.rml --root . --screenshot hello.png
```

Run with `--strict` before you commit a document. It catches unreachable focus, missing sprites and
properties that parse but do nothing — the class of mistake that otherwise shows up as a screen that
looks *almost* right.

## 7. Make it survive packaging

Documents, stylesheets, fonts and images are **files, not assets**. The cooker knows nothing about
them, so unless you say otherwise they are simply absent from a package and every screen comes up
empty in a build that worked in the editor. Add this to `Config/DefaultGame.ini` before you package
anything:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
; Folio's distance field material is loaded by path, so nothing references it and the cooker never
; sees it. Without this, all text renders with no shader.
+DirectoriesToAlwaysCook=(Path="/Folio")
+DirectoriesToAlwaysStageAsUFS=(Path="../UI")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/rcss")
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/fonts")
```

The `../` on the staging lines is not a mistake. These paths are resolved against `Content`, not the project root, and the
staged path collapses back to the project root — which is exactly where Folio looks at runtime. Drop
the `fonts` line if you ship your own faces in `UI/fonts`.

List your own subdirectories rather than `UI` wholesale if the tree holds things a player should not
receive. **There is no way to exclude a subdirectory of one you have staged**, which is why Folio's
own demo documents live in `Plugins/Folio/examples` rather than under `rcss` - staging the stylesheets
a game needs would otherwise stage the demos with them.

The other side of that: **point anything at `/folio/examples/` and it will not be there in a packaged
build.** It works in the editor and in a standalone run, which read loose files, and then renders
nothing once packaged. Add the line while you are prototyping against them, and take it out before
you ship:

```ini
+DirectoriesToAlwaysStageAsUFS=(Path="../Plugins/Folio/examples")
```

To check it worked, look for your files in the staging manifest rather than launching the build:

```powershell
Select-String -Path "$env:LOCALAPPDATA\..\..\<engine>\Engine\Programs\AutomationTool\Saved\Logs\FinalCopyWin64_UFSFiles.txt" -Pattern "/UI/"
```

### Changing UI on a device without cooking again

Launch a Development or Test build with `-uidev` and Folio reads from a `uidev` directory in the
project — in a staged build that is `<Staged>/<YourProject>/uidev`, beside the `UI` tree — before it
reads the package. `-uidev=<path>` points somewhere else. The directory mirrors
the project, so `uidev/UI/screens/hud.rml` overrides `UI/screens/hud.rml`, and anything absent falls
through to the package — a directory holding one document is enough. This is how UI gets iterated on
a console or a Steam Deck. It does not exist in Shipping.

## Where to go next

- **[What RCSS actually supports](../UI_CONVENTIONS.md).** The authoritative list of what works, what
  is silently ignored, and what is banned outright. Read it before authoring anything real; RmlUi is
  close enough to CSS to mislead.
- **[Hosting a document](hosting.md)** — `UFolioWidget`, focus, and fitting into an existing widget tree.
- **[Data binding](data_binding.md)** — showing values from your game, and whether a view model is worth it.
- **[Generating a view model](view_models.md)** — the C++ behind a document's bindings, generated from
  the document and its mock.
- **[Setting up VSCode](vscode.md)** — highlighting and a live preview beside the document, and what
  the preview is and is not worth trusting.
- **[Documents in the world](world_documents.md)** — a document on a surface in the level, the three
  ways it can reach the screen, and why it is not a `UWidgetComponent`.
- **[Localization](localization.md)** — keying your text, and getting it into Unreal's gather.
- **[Performance and profiling](performance_profiling.md)** — `stat Folio`, Insights, and why a settled
  document costs nothing.

## If nothing appears

- **No text at all, everything else fine.** No font registered. Check the log for
  `no font faces registered`.
- **`{{ braces }}` rendered literally.** The document declares `data-model` but nothing is bound. Add
  `UI/mock/<model>.json` for a design-time stand-in.
- **The window opens empty.** Check the path: `folio.ShowWindow` resolves relative to the project
  directory, so `UI/hello.rml`, not `Content/UI/hello.rml` and not an absolute path.
- **Fine in the editor, empty in a packaged build.** The files were not staged. See step 7.
- **An edit is ignored in a packaged build.** A stale copy in the `-uidev` override directory. Folio
  logs a warning naming the directory at startup for exactly this reason.
- **Anything surprising.** [Internals](internals.md) is a ledger of things that have already
  caught someone out.

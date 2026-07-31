# Folio preview for VSCode

Renders the open `.rml` beside it, and re-renders on save.

## Installing

No build step and no dependencies — the extension is one JavaScript file. Symlink or copy this
directory into your VSCode extensions folder and reload the window:

```bash
# Windows
mklink /D "%USERPROFILE%\.vscode\extensions\folio-preview" "<repo>\Plugins\Folio\Tools\vscode-folio"

# Linux and macOS
ln -s <repo>/Plugins/Folio/Tools/vscode-folio ~/.vscode/extensions/folio-preview
```

Then open a document and run **Folio: Open Preview** (`ctrl+shift+v`).

It finds `uipreview` under `Plugins/Folio/bin/` in the workspace. Build it first with
`./scripts/build.sh`, or point `folio.uipreviewPath` at it.

## What it is, and what it is worth trusting

A webview over `uipreview`. The renderer already exists, already watches files and already
screenshots; a second renderer in a webview would be a second thing to keep honest, and the whole
value of a preview is that it shows what the game will show.

**Layout is proven to match the engine.** `./scripts/compare_renderers.sh` renders every document
through both renderers and compares where each text line landed. It passes exactly - not within a
tolerance - across the documents the game ships.

**The appearance of layer effects is not.** `box-shadow`, `filter`, `backdrop-filter` and
`mask-image` are drawn in OpenGL here and replayed through RHI in the engine, and nothing compares
those pixels. A document using them gets a note in the panel saying so. Layout under an effect is
still exact; a shadow's colour or blur radius is not something this can promise.

**Colour will differ from a screenshot of the game**, always. The preview does no sRGB conversion
where Slate linearises vertex colour and applies display gamma. Judge colour in the engine.

## Settings

| | |
|---|---|
| `folio.uipreviewPath` | Path to the binary. Empty searches the workspace. |
| `folio.root` | Passed as `--root`. Empty uses the workspace folder. |
| `folio.size` | Render size, default `1280x720`. |
| `folio.dpRatio` | Pinned dp ratio, default `1`. Set it to whatever the game uses, or every length here differs from the game's. |
| `folio.strict` | Report warnings rather than rendering past them. On by default. |
| `folio.culture` | `pseudo` inflates strings and accents them, which finds overflow before translation exists. |
| `folio.themes` | Themes to activate, matching `folio.Theme` in the engine. |

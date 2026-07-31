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

## Zooming

`Ctrl` and the wheel, the toolbar buttons, or `Ctrl` with `+`, `-` and `0`.

**Zooming re-renders.** It scales the dp ratio rather than magnifying the image, which is what a
browser's zoom does: the viewport stays the size you configured and the content within it grows, so
you see less of the document at more detail. Text and vector art get *sharper* as they grow, because
they are distance fields - which is the only way a preview can tell you whether the real thing is
crisp. Scaling a screenshot would show you the scaler's work instead.

## Editing

Click an element to select it and jump to the rule that styles it. Drag it to move it, and pick which
rule the move should edit - `.btn` moves every button, `#continue` moves one.

Edits are a protocol rather than a rewrite: one property, one rule, applied in place with every
comment and indent preserved, through the editor's undo stack. `rcss.js` does that work and
`rcss.test.js` covers it; run `npm test`, or `node rcss.test.js`.

Put the caret in a rule in an `.rcss` and the elements it reaches light up in the preview.

## Syntax highlighting

`.rml` and `.rcss` get highlighted on install. Both reuse VSCode's built-in grammars - XML for markup,
CSS for stylesheets - rather than shipping two grammars to keep in step with them.

They are contributed as their own language ids rather than as extra extensions on `xml` and `css`,
which matters: VSCode's CSS validator is bound to the `css` language, and RCSS has properties it does
not know - `decorator`, `nav`, `font-effect`, `drag` - and `dp` units it would flag. Registering
`.rcss` as CSS would put a red squiggle under most of a correct stylesheet.

A `<style>` block inside a document is highlighted as RCSS rather than as markup, since that is the
one file where both languages meet.

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

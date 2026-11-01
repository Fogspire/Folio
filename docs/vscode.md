# Setting up VSCode

`.rml` and `.rcss` highlighted in VSCode, with a `<style>` block inside a document highlighted as RCSS.

The extension also has a preview, which is deprecated: use [Folio Studio](studio.md) to see a document.

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

## 2. Open a document

Open any `.rml` or `.rcss`. Both highlight as soon as the extension is installed, reusing VSCode's built-in grammars (XML for markup, CSS for stylesheets).

## If nothing is highlighted

- **The extension is not there after installing.** Reload the window. VSCode scans for extensions once at startup.
- **A stylesheet shows no CSS completions.** Expected: `.rcss` is its own language, not CSS. VSCode's CSS validator does not know `decorator`, `nav`, `font-effect`, `drag` or `dp` units and would flag most of a correct stylesheet.

## Where to go next

- **[What RCSS actually supports](../UI_CONVENTIONS.md)**: read it before authoring anything real. RmlUi is close enough to CSS to mislead.

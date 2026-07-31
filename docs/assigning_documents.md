# Assigning a document in the editor

Every Folio component takes a **Document Path** rather than an asset reference, because a document is
text a person edits rather than something to import. The editor makes that behave like a picker.

## The picker

Click the field and you get a collapsible tree of every `.rml` under the project and the plugin, with
a filter box focused on open.

- **Typing filters** on the whole path, so `screens` finds a directory as readily as `hud` finds a
  document. A filtered tree opens fully — a match three levels down is no use collapsed.
- **Unfiltered, it opens to the current selection** and scrolls it into view.
- Directories are drawn quieter than documents, because a directory is not a choice.

## Absolute paths are rewritten

Paste or drag in an absolute path and it is rewritten to the portable form the moment the field
commits. Under the project it becomes `ui/screens/hud.rml`; under the plugin it becomes
`/folio/examples/...`, which resolves from any content root.

This is not cosmetic. **An absolute path is wrong the moment anyone else opens the level**, and the
failure is a screen that renders nothing on their machine and works perfectly on yours.

Plugin mounts are tested before the project root, so a plugin document does not become
`Plugins/Folio/examples/...` — that would resolve only from this project rather than from any.

## A path that names nothing

A warning icon appears in the field, with the resolved path in its tooltip.

The same thing is reported when the document is loaded, and it names what it tried:

```
Folio: 'ui/screens/hud.rml' does not exist. It resolved to 'X:/Game/ui/screens/hud.rml'.
Folio: a relative path is read against the content root, which is 'X:/Game'.
Folio: documents that do exist:
    ui/screens/settings.rml
    ui/hud/nameplate.rml
    ...
```

The list is there because the answer is almost always a typo against something in it, and printing it
turns a guess into a comparison.

**A missing document and a document that fails to parse are reported differently.** Both render
nothing, and telling them apart by looking is impossible.

## Resetting the field

The reset arrow restores the class default, as it does anywhere else — **unless that default names a
document that no longer exists**, in which case it clears the field instead.

A default pointing at a deleted or renamed document is worse than no default: resetting puts back a
path that cannot load, and the field then looks deliberately set. An empty path is legal, since a
component may be given its document from code.

The arrow appears whenever the field differs from what resetting would produce, so a field already
holding a dead default shows as modified.

## Letting a document set its own size

A document can declare the size it was designed for, and picking it fills in the component's size
property:

```html
<head>
	<title>CRT terminal</title>
	<meta name="folio-size" content="1024x640"/>
</head>
```

| Component | Property it fills |
|---|---|
| `UFolioWidget` | `Desired Size`, in pixels. |
| `UFolioMarkerDocument` | `Size`, in pixels. |
| `UFolioWorldDocument` | `World Size`, in world units — the declared pixels divided by `Pixels Per Unit`. Set that first. |

**Declared, not measured.** A document has no intrinsic size to read: RCSS is written against whatever
box it is given and `body` is almost always `100%`, so laying one out to ask how big it is would only
ever return the size it was already laid out at. Asking the markup is the only answer that is not a
guess.

Nothing is filled in for a document that stays silent, and **nothing is filled in over a size that has
already been changed from its default** — only an untouched field is written. That matters most for
`UFolioWorldDocument`, whose `World Size` is the mesh it sits on: it is measured against geometry the
document knows nothing about, and replacing it with whatever the markup declares would be worse than
filling nothing in.

Both numbers must be positive; a malformed value is logged and ignored rather than half applied.

## Hiding things from the picker

**Project Settings → Plugins → Folio**:

| | |
|---|---|
| `Hidden Document Directories` | Prefixes, relative to the project. One entry hides a tree. |
| `Show Plugin Documents In Picker` | On by default. Turn it off once the game has its own screens and the examples are only in the way. |

A game accumulates markup that is not a screen anyone opens — fragments, templates, a scratch
directory — and a picker listing all of it is as unhelpful as no picker.

## From code

The path is an `FFolioDocumentPath`, which reads and writes as a string everywhere: assignment,
comparison and the Blueprint node all take an `FString`, and `*DocumentPath` is a `TCHAR*` exactly as
`*FString` is.

```cpp
Document->LoadDocument(TEXT("ui/screens/hud.rml"));
```

`UFolioDocumentPathLibrary` exposes the same helpers the editor uses, and they are Blueprint-callable:

| | |
|---|---|
| `ResolveDocumentPath` | The absolute path Folio will open, or empty. |
| `DocumentExists` | Whether it names a file. |
| `MakeDocumentPathPortable` | Absolute to project-relative, leaving anything already portable alone. |
| `GetDeclaredDocumentSize` | The `folio-size` the markup declares, or false if it declares none. |

A level saved when this property was a plain `FString` still loads — the struct reads the old value.

## Where to go next

- **[Hosting a document](hosting.md)** — `UFolioWidget` in an existing widget tree.
- **[Documents in the world](world_documents.md)** — the world component's own properties.
- **[Nameplates and markers](markers.md)** — the marker component's.

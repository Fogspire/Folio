# Assigning a document in the editor

Every Folio component takes a **Document Path** rather than an asset reference, because a document is text rather than an import. The editor shows it as a picker.

## The picker

Click the field and you get a collapsible tree of every `.rml` under the project and the plugin, with a filter box focused on open.

- **Typing filters** on the whole path, so `screens` finds a directory as readily as `hud` finds a document. A filtered tree opens fully.
- **Unfiltered, it opens to the current selection** and scrolls it into view.

## Absolute paths are rewritten

Paste or drag in an absolute path and it is rewritten to the portable form when the field commits. Under the project it becomes `UI/screens/hud.rml`; under the plugin it becomes `/folio/examples/...`, which resolves from any content root.

**An absolute path is wrong the moment anyone else opens the level**: the screen renders nothing on their machine and works on yours.

Plugin mounts are tested before the project root, so a plugin document does not become `Plugins/Folio/examples/...`, which would resolve only from this project.

## A path that names nothing

A warning icon appears in the field, with the resolved path in its tooltip.

Loading the document reports it too, naming what it tried:

```
Folio: 'UI/screens/hud.rml' does not exist. It resolved to 'X:/Game/UI/screens/hud.rml'.
Folio: a relative path is read against the content root, which is 'X:/Game'.
Folio: documents that do exist:
    UI/screens/settings.rml
    UI/hud/nameplate.rml
    ...
```

The answer is almost always a typo against something in the list.

**A missing document and a document that fails to parse are reported differently.** Both render nothing.

## Finding them in a level

Every report names the component *and its actor*, and appears as a clickable entry in **Window > Message Log > Folio**. Map Check and asset validation carry the same tokens, so the entry selects the actor and opens the asset wherever the mistake surfaces.

For a level with more than one, the **Folio** button in the level toolbar has **Select Actors With Broken Documents**. It selects every actor holding a document component whose path names nothing, or that has no path at all. After a directory rename, selecting them all puts them in the details panel together, where one edit to the shared property fixes the set. The selection is transacted, so undo restores the previous selection.

The result arrives as a notification counting each kind, with details in the log.

## A path that will not survive packaging

A document outside every directory listed under **Project Settings > Packaging > Additional Non-Asset Directories to Package** is reported as a *warning* rather than an error: the editor and a standalone run both read loose files, and it is only missing once packaged.

The **Folio** toolbar menu has **Stage Documents Used In This Level**, which resolves every document path in the level to the tree it lives in and adds any that packaging would miss to `DefaultGame.ini`. Trees rather than the documents' own directories, because a document links stylesheets and art beside it. It lists what it added in the Message Log.

Folio's `examples/` sit outside `rcss/` so that a game does not ship the demos, so a component pointing at `/folio/examples/` renders nothing in a packaged build. Stage that directory while prototyping against them; see [hello world](hello_world.md).

## Resetting the field

The reset arrow restores the class default, **unless that default names a document that no longer exists**, in which case it clears the field. An empty path is legal, since a component may be given its document from code.

The arrow appears whenever the field differs from what resetting would produce, so a field already holding a dead default shows as modified.

## Letting a document set its own size

A document can declare the size it was designed for, and picking it fills in the component's size property:

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
| `UFolioWorldDocument` | `World Size`, in world units: the declared pixels divided by `Pixels Per Unit`. Set that first. |

**Declared, not measured.** A document has no intrinsic size: `body` is almost always `100%`, so laying one out returns the size it was laid out at.

Nothing is filled in for a document that declares no size, and **nothing is filled in over a size already changed from its default**. That matters most for `UFolioWorldDocument`, whose `World Size` is measured against the mesh it sits on.

Both numbers must be positive; a malformed value is logged and ignored rather than half applied.

## Hiding things from the picker

**Project Settings → Plugins → Folio**:

| | |
|---|---|
| `Hidden Document Directories` | Prefixes, relative to the project. One entry hides a tree. |
| `Show Plugin Documents In Picker` | On by default. Turn it off once the game has its own screens and the examples are only in the way. |

Use these for markup that is not a screen (fragments, templates, a scratch directory).

## From code

The path is an `FFolioDocumentPath`, which reads and writes as a string everywhere: assignment, comparison and the Blueprint node all take an `FString`, and `*DocumentPath` is a `TCHAR*` exactly as `*FString` is.

```cpp
Document->LoadDocument(TEXT("UI/screens/hud.rml"));
```

`UFolioDocumentPathLibrary` exposes the editor's helpers, Blueprint-callable:

| | |
|---|---|
| `ResolveDocumentPath` | The absolute path Folio opens, or empty. |
| `DocumentExists` | Whether it names a file. |
| `MakeDocumentPathPortable` | Absolute to project-relative, leaving anything already portable alone. |
| `GetDeclaredDocumentSize` | The `folio-size` the markup declares, or false if it declares none. |

A level saved when this property was a plain `FString` still loads: the struct reads the old value.

## Where to go next

- **[Hosting a document](hosting.md)**: `UFolioWidget` in an existing widget tree.
- **[World document settings](world_document_settings.md)**: the world component's own properties.
- **[Nameplates and markers](markers.md)**: the marker component's.

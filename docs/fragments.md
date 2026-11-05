# Fragments: UI that objects add and remove

A base HUD with an empty slot, and each object the player holds or possesses putting its own panel into that slot, in its own file with its own styles and view model, and taking it out again.

## Add one

1. Give the base HUD an element to put fragments in:

   ```html
   <body data-model="hud">
   	<div class="status">{{ Health }}</div>
   	<div id="held"></div>
   </body>
   ```

2. Write the fragment as an ordinary document. Its head's stylesheets and its body are what get used:

   ```html
   <!-- UI/held/rifle.rml -->
   <rml>
   <head>
   	<link type="text/rcss" href="rifle.rcss"/>
   </head>
   <body class="rifle-panel" data-model="rifle">
   	<div class="ammo">{{ Ammo }} / {{ Reserve }}</div>
   </body>
   </rml>
   ```

3. From the object, add it when it is picked up or possessed and remove it when it is let go:

   ```cpp
   const int32 Handle = Widget->AddFragment(TEXT("held"), TEXT("UI/held/rifle.rml"), Model);

   Widget->RemoveFragment(Handle);
   ```

   In Blueprint these are **Add Fragment** and **Remove Fragment** on a **Folio Document**. A **Folio World Document** has the same two.

The fragment's body is appended to the slot inside one `div` that carries the body's attributes, here `class="rifle-panel"`. Its stylesheets join the HUD's. The view model is bound to the model the body names, `rifle`, and is released when the fragment is removed.

A fragment with no `data-model` on its body reads the model in scope at its slot, so the same fragment could show `{{ Health }}` from the HUD's model.

Fragments can be added before the document loads, and they come back after a hot reload or a rebuild, in the order they were added.

## Preview it

`uipreview` places fragments with `--fragment <slot id>=<path>`, repeatable:

```bash
./Plugins/Folio/bin/win64/uipreview.exe UI/screens/hud.rml --root . --fragment held=UI/held/rifle.rml --screenshot hud.png
```

A fragment's model reads its mock from `UI/mock/<model>.json`, as a document's does. Studio shows the HUD without its fragments; open a fragment on its own to edit it.

## If it does not appear

| Symptom | Cause |
|---|---|
| `fragment '...' names slot 'held', and nothing in '...' has that id` | No element has that id when the fragment is placed. The slot is looked up when the fragment is added and each time the document loads. |
| `fragment '...' could not be read` | The path resolves the way a document path does: relative to the project, or `/folio/` for the plugin. |
| `fragment '...' was given a view model, but its body names no data-model` | Put `data-model="name"` on the fragment's `<body>`. |
| `fragment '...' names data-model "rifle", which is already bound` | Two fragments on screen at once use one model name, so both show the first one's data. Give each its own name. |
| Its styles change other parts of the HUD | A fragment's stylesheets apply to the whole document. Scope its selectors under the body's class: `.rifle-panel .ammo`. |

## Details

- `AddFragment` returns 0 for an empty slot id or path. Any other number is the handle; `RemoveFragment` returns false for one it does not know.
- Later fragments' stylesheets come after earlier ones', so they win at equal specificity.
- Ids in a fragment share the document with the HUD's, so keep them unique across both.
- `data-loc` text in a fragment is translated in the fragment's own namespace, derived from its path as a document's is.

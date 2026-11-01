# Drag, drop and resize

Moving a window, dropping an icon in a bin, resizing a panel by its corner. All of it works with no code; the parts that need code are named here.

![Draggable, resizable terminal windows and a recycle bin](https://github.com/user-attachments/assets/f1d531cd-b46c-4e55-b437-45c3d3ed54bf)

## Moving and resizing: no code at all

`<handle>` is a real element type and does both:

```html
<div id="win" class="window">
	<handle class="titlebar" move_target="win">Title</handle>
	<div class="body">…</div>
	<handle class="grip" size_target="win"></handle>
</div>
```

```css
/* The target must be absolutely positioned. A handle writes left, top, width and height, and
   those mean nothing to a flex item. */
.window   { position: absolute; }
.titlebar { cursor: move; }
.grip     { position: absolute; right: 0; bottom: 0; width: 18dp; height: 18dp; cursor: nwse-resize; }
```

The target is named by id, so a handle can move or resize anything, not only its parent. `edge_margin` on the handle keeps the target inside its parent. `move_target` and `size_target` are RmlUi's own.

## Dragging: `drag` fires events, it moves nothing

RmlUi's `drag` property decides whether an element can be *picked up*; it reports the gesture and leaves the element where it was.

```css
.icon { drag: drag-drop; }   /* it can now be dragged. It still does not move. */
```

`folio-drag="move"` repositions it:

```html
<div class="icon" drag="drag-drop" folio-drag="move" style="left: 28dp; top: 66dp;">CORE.DAT</div>
```

```css
.icon { position: absolute; }
```

The element must be `position: absolute`, because `folio-drag` writes `left` and `top`. It warns if you forget.

`drag` values: `none` (the default), `drag`, `drag-drop`, `block`, `clone`.

**Use `drag-drop`, not `drag`, on anything you mean to drop.** RmlUi only looks for a drop target when the dragged element's `drag` is `drag-drop` or `clone`. With plain `drag` the zone never hears about it and `folio-drop` never runs. This is the most common reason a drop does nothing.

## Dropping

```html
<div class="bin" folio-drop="hide">
	<img class="bin-icon" sprite="recycle"/>
	<span>RECYCLE</span>
</div>
```

| | |
|---|---|
| `folio-drop="hide"` | Hides the dropped element |
| `folio-drop="remove"` | Removes it from the document |
| `folio-drop="accept"` | Does nothing itself, but still gets `folio-dragover` for styling |

**The attribute is looked for up the tree, not on the event's target.** An event targets the deepest element under the pointer, so dropping on that bin means dropping on its icon or its label. Put `folio-drop` on the box you think of as *the thing*; children need nothing.

Only an element with `folio-drop` gets `folio-dragover`.

## The state classes

```css
.icon.folio-dragging { opacity: 0.55; }
.bin.folio-dragover  { border-color: #ffffff; background-color: #ffffff14; }
```

| | |
|---|---|
| `folio-dragging` | On the dragged element for the length of the drag |
| `folio-dragover` | On a drop zone while a drag is over it |
| `folio-closing` | On anything on its way out, by a close **or** a drop |

Style `folio-dragover`. Without it a player cannot tell whether a drop lands, and the feature reads as broken.

## Closing, and animating something out

```html
<div class="close" folio-close="win" folio-label="Close">X</div>
```

`folio-close="id"` closes that element on click; with no id it closes its parent. Like `folio-drop`, the attribute is found up the tree.

**`hide`, `remove` and `close` all animate.** Each puts `folio-closing` on the element and *waits*:

```css
@keyframes suck { to { transform: translateY(520dp) scale(0.04); opacity: 0; } }
.window.folio-closing { animation: 0.5s cubic-in suck; }

@keyframes consume { to { transform: scale(0.05); opacity: 0; } }
.icon.folio-closing { animation: 0.35s cubic-in consume; }
```

Without an animation on the class, the element goes at once. With one, the removal waits for it to finish.

On a **world-space surface** the render target *is* the document, so anything translated past its edge is clipped, and text that leaves the context is culled outright. An exit that slides off screen vanishes partway. Shrink towards an edge instead.

## Where the game takes over

Anything beyond those three actions is yours:

```cpp
// C++: listen for the event.
Element->AddEventListener("dragdrop", Listener);
```

```html
<!-- Or against a bound model. -->
<div class="slot" data-event-dragdrop="MoveItem(it_index)"></div>
```

The event carries the dragged element, so an inventory can validate a move, swap two stacks or reject a drop and keep the presentation in the stylesheet.

The behaviours live in `FolioCore`, which both the preview tool and the Unreal host install, so a drag behaves identically in `uipreview` and in the game.

## A drag is a pointer gesture

It works on a **world-space document** exactly as on screen, because both feed the same press, move and release. See **[documents in the world](world_documents.md)**.

Drag has no touch or gamepad equivalent. If the interaction has to work on a pad, give it a non-drag path too (pick up with confirm, move with the stick, place with confirm).

## Worked example

`examples/crt_terminal.rml` has all of it in one document: a draggable window with a title-bar handle, a corner grip, two icons that drag and drop into a bin, and an animated close, on an oval CRT screen in the world. The **[CRT terminal tutorial](tutorials/crt_terminal.md)** builds it from nothing.

## Where to go next

- **[Animation](animation.md)**: `folio-closing` and what is cheap to animate.
- **[Input and navigation](input_and_navigation.md)**: the keyboard and pad path.
- **[UI_CONVENTIONS.md](../UI_CONVENTIONS.md)** §12: the full reference.

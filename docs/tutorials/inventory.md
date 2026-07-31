# An inventory screen

A grid of slots, items you can drag between them, a detail pane that follows the selection, and full
gamepad navigation. Built from a data model, so the markup never knows what an item is.

This is the second tutorial. It assumes you have read
**[the CRT terminal](crt_terminal.md)** or already know the drag vocabulary.

---

## 1. The model first

An inventory is a list, and a list means `data-for`. Decide the shape before writing markup, because
the markup is a direct expression of it:

```cpp
UCLASS()
class UInventoryModel : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY() TArray<FInventoryEntry> Slots;
	UPROPERTY() int32 Selected = 0;
	UPROPERTY() FString DetailName;
	UPROPERTY() FString DetailText;
};

USTRUCT()
struct FInventoryEntry
{
	GENERATED_BODY()

	UPROPERTY() FString Sprite;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() bool bEmpty = true;
};
```

Bind it before the document loads - RmlUi resolves a document's bindings as it loads and offers no
way to attach a model afterwards:

```cpp
Document->BindViewModel(TEXT("inventory"), Model);
Document->LoadDocument(TEXT("UI/screens/inventory.rml"));
```

For previewing, put the same shape in `UI/mock/inventory.json` and the preview picks it up
automatically. Build the whole screen against the mock before any of the C++ exists.

See **[data binding](../data_binding.md)**.

---

## 2. The grid

```html
<div class="grid" data-model="inventory">
	<div class="slot focusable" data-for="entry : Slots"
		data-class-empty="entry.bEmpty"
		data-class-selected="it_index == Selected"
		data-event-click="Selected = it_index">

		<img class="slot-icon" data-attr-sprite="entry.Sprite" data-if="!entry.bEmpty"/>
		<span class="slot-count" data-if="entry.Count &gt; 1">{{ entry.Count }}</span>
	</div>
</div>
```

Three things worth pointing at:

**`data-attr-sprite`, not `sprite="{{ }}"`.** Interpolation is text-only and only inside an element's
text content. Attributes need `data-attr-*`.

**`it_index` is free inside a `data-for`** and is the zero-based index. It is how a repeated element
knows which one it is.

**`&gt;` inside an attribute.** It is XML - a bare `>` in an attribute value is a parse error.

The grid itself is flexbox with wrapping, because there is no `display: grid`:

```css
.grid {
	display: flex;
	flex-wrap: wrap;
	gap: 8dp;
	width: 100%;
}

.slot {
	width: 72dp;
	height: 72dp;
	flex: 0 0 auto;
	box-sizing: border-box;
	position: relative;
	background-color: #1f1f28;
	border: 2dp #ffffff1a;
	border-radius: 6dp;
}
```

`flex: 0 0 auto` and a fixed size, so wrapping produces a grid rather than stretching a row.

---

## 3. Navigation

A wrapping grid is exactly the case RmlUi's spatial navigation is good at - it finds the nearest
element in the direction pressed, which is what a grid wants and what an index-based scheme gets
wrong at the row ends.

```css
.slot { tab-index: auto; nav: auto; }
body  { nav: auto; }
```

`nav: auto` rather than `vertical`, because a grid navigates in all four directions.

**`nav` goes on the slot, not on the grid.** A container's `nav` is never consulted. This is the
single most common reason a menu moves one step and freezes. See
**[input and navigation](../input_and_navigation.md)**.

**Watch `overflow` on the scroll container.** If the grid scrolls, the spatial search is confined to
the nearest scroll container - which is what you want here, and worth knowing so it does not surprise
you when focus refuses to leave.

Style the focus separately from the selection, because they are different things:

```css
.slot:focus-visible { border-color: #f0c674; box-shadow: #f0c67459 0dp 0dp 12dp; }
.slot.selected      { background-color: #2b2b36; border-color: #ffffff40; }
```

---

## 4. The detail pane

```html
<div class="detail" data-if="!Slots[Selected].bEmpty">
	<h2>{{ DetailName }}</h2>
	<p class="t-caption c-dim">{{ DetailText }}</p>
</div>

<div class="detail empty" data-if="Slots[Selected].bEmpty">
	<p class="c-dim" data-loc="inv_empty">Nothing selected</p>
</div>
```

**There is no `data-else`.** Write the complementary `data-if`.

`data-if` adds and removes the element; `data-visible` only toggles `visibility` and keeps the box.
Use `data-if` when the empty state should not reserve space, `data-visible` when the layout should
not jump.

`{{ Slots.size }}` gives the array's length, which is what an "empty inventory" message tests:

```html
<p data-if="Slots.size == 0" data-loc="inv_none">Your pack is empty.</p>
```

---

## 5. Dragging between slots

```html
<div class="slot focusable" data-for="entry : Slots" drag="drag-drop" folio-drop="accept">
```

Two attributes and the gesture works. But **an inventory move is a game decision** - swapping two
stacks, rejecting a drop onto a locked slot, splitting a stack - so this is where Folio's built-in
actions stop and yours start:

```html
<div class="slot" ... data-event-dragdrop="MoveItem(it_index)"></div>
```

or in C++:

```cpp
Slot->AddEventListener("dragdrop", Listener);
```

`folio-drop="accept"` does nothing itself but still gets the `folio-dragover` class, which is what
you want here: the styling is Folio's, the decision is yours.

```css
.slot.folio-dragover { border-color: #f0c674; background-color: #f0c6741a; }
.slot.folio-dragging { opacity: 0.55; }
```

**A controller cannot drag.** If the inventory has to work on a pad, give it a second path - confirm
to pick up, direction to move, confirm to place - driven from the same `MoveItem`. The drag is an
affordance for mouse users, not the only route.

---

## 6. Counts, weights and bars

Anything numeric goes through `format`, or you get `72.000000`:

```html
<span class="t-numeric">{{ Weight | format(1) }} / {{ MaxWeight | format(0) }} kg</span>
```

An encumbrance bar is a width from an expression:

```html
<div class="bar">
	<div class="bar-fill"
		data-style-width="Weight / MaxWeight * 100 + '%'"
		data-class-over="Weight &gt; MaxWeight"></div>
</div>
```

The result needs units, hence `+ '%'`. And `data-style-*` writes an **inline style**, which a
stylesheet-only hot reload will not touch - fine for a value, wrong for anything you might want to
retheme. Colour it with `data-class-*` instead:

```css
.bar-fill      { transition: width 0.15s cubic-out, background-color 0.3s cubic-out; }
.bar-fill.over { background-color: #e06c75; }
```

---

## 7. Localise and label as you go

```html
<h1 class="t-title" data-loc="inv_title">Inventory</h1>
<span class="slot-count" data-noloc>{{ entry.Count }}</span>
<div class="slot focusable" folio-label="Inventory slot" ...>
```

`data-loc` on everything the player reads, `data-noloc` on numerals and names, `folio-label` on the
slots - which have no text of their own and would otherwise be nameless.

Item names come from the model and were already `FText` in C++, so they need neither.

---

## 8. Check it

```bash
./bin/win64/uipreview.exe UI/screens/inventory.rml --root . --strict --a11y
./bin/win64/uipreview.exe UI/screens/inventory.rml --root . --culture pseudo --strict
./bin/win64/uipreview.exe UI/screens/inventory.rml --root . --keys "right,right,down,enter" --shot-each out/
```

The pseudo run is the one that catches an inventory laid out for English. Translated strings run
about 40% longer, and a slot count or an item name that fits at 72dp will not at 100.

The `--keys` run is how you prove the grid navigates without picking up a controller - and it is
worth doing, because a grid that navigates *almost* right is very hard to spot by looking.

---

## What you have learned

- shaping a model so the markup falls out of it (§1)
- `data-for`, `it_index`, and `data-attr-*` versus `{{ }}` (§2)
- a wrapping flex grid, since there is no `display: grid` (§2)
- spatial navigation over a grid, and where `nav` goes (§3)
- complementary `data-if`, and `.size` on an array (§4)
- where Folio's drag support ends and the game's rules begin (§5)
- `format()`, and why `data-style-*` is a last resort (§6)

## Where to go next

- **[Data binding](../data_binding.md)** — the model side in full.
- **[Generating a view model](../view_models.md)** — when reflection is not enough.
- **[A list of ten thousand items](long_lists.md)** — when the list is too long for this approach.
- **[Drag, drop and resize](../drag_and_resize.md)** — the complete vocabulary.

# Showing game data: a health bar

A bar and a number, driven by a float on your character. By the end you will have something
functional, and a clear idea of which binding approach to reach for next time.

Assumes you have finished [hello world](hello_world.md).

## The shape of it

A document never reaches into your game. Instead, game code hands Folio **one object per screen**, and
Folio exposes that object's properties to the markup as a *data model*:

```
your gameplay  →  a boundary object  →  data model  →  {{ Health }} in markup
```

That boundary object is the only thing the two sides agree on. Keep it small and keep it about the
screen, not about the game: a health bar wants `Health` and `MaxHealth`, not a pointer to your
character.

**This is the rule that matters.** Which base class the object uses is a much smaller decision, and
one you can change later without touching the markup.

## 1. Add something to show

Put a couple of floats on your character. Any `AActor` or `UObject` will do:

```cpp
UPROPERTY(BlueprintReadOnly, Category = "Vitals")
float Health = 100.0f;

UPROPERTY(BlueprintReadOnly, Category = "Vitals")
float MaxHealth = 100.0f;
```

`BlueprintReadOnly` matters. Folio only binds properties marked `BlueprintReadOnly`,
`BlueprintReadWrite` or `EditAnywhere` — internal state is not a binding surface, so a plain
`UPROPERTY()` stays invisible to markup on purpose.

## 2. Write the document

`<YourProject>/UI/healthbar.rml`:

```html
<rml>
<head>
	<title>Health</title>
	<link type="text/rcss" href="/folio/folio.rcss"/>
	<style>
		body { background-color: #1f1f28; color: #e8e8f0; font-size: 18dp; padding: 24dp; }

		.health { width: 260dp; }
		.readout { margin-bottom: 6dp; }
		.track { width: 260dp; height: 16dp; background-color: #33333d; border-radius: 8dp; }
		.fill { height: 16dp; background-color: #63c98a; border-radius: 8dp; }

		.health.low .fill { background-color: #d9534f; }
	</style>
</head>
<body data-model="player">
	<div class="health" data-class-low="Health / MaxHealth &lt; 0.25">
		<div class="readout">{{ Health | format(0) }} / {{ MaxHealth | format(0) }}</div>
		<div class="track">
			<div class="fill" data-style-width="Health / MaxHealth * 100 + '%'"></div>
		</div>
	</div>
</body>
</rml>
```

`data-model="player"` names the model this subtree reads from. Everything else is doing one of three
jobs:

| | |
|---|---|
| `{{ Health \| format(0) }}` | Prints a value. `format(value, digits)` rounds a float for display; without it you get `72.000000`. |
| `data-style-width="..."` | Computes a style property. Arithmetic works, and the result needs units, so `+ '%'`. |
| `data-class-low="..."` | Adds or removes a class from a condition. Let RCSS decide what "low" *looks* like. |

That last row is the habit worth forming. Bind a **condition**, not a colour — the document decides
appearance, and your C++ never mentions red.

Note `&lt;` for the less-than sign, since this is inside an XML attribute.

## 3. See it before writing any C++

Folio looks for `UI/mock/<model>.json` and mounts it if game code has not bound anything. Create
`<YourProject>/UI/mock/player.json`:

```json
{ "Health": 72.0, "MaxHealth": 100.0 }
```

Now the preview shows a working bar:

```bash
./bin/win64/uipreview.exe <YourProject>/UI/healthbar.rml --root <YourProject>
```

Edit the numbers, save, watch it change. Set `Health` to `18` and the bar turns red without any code
running. This is the fastest way to build a screen, and it keeps working forever as a way to see
states that are awkward to reach in game.

## 4. Bind the real thing

Host the document with `UFolioWidget` and bind your object to the same model name:

```cpp
Document->BindViewModel(TEXT("player"), MyCharacter);
```

That is the whole connection. `Health` and `MaxHealth` are found by reflection, so nothing lists the
properties twice, and the mock file steps aside automatically once something is bound.

Bind before the document loads if you can, so bindings resolve on the first frame rather than showing
literal braces for one.

## Choosing how the data updates

The one real decision. Folio supports two, and the call site above is identical for both.

**Notified.** If your object implements `INotifyFieldValueChanged`, Folio subscribes per field and
only touches the document when a value actually changes. `UMVVMViewModelBase` implements it, so any
MVVM view model gets this automatically.

**Polled.** Otherwise Folio re-reads every bound property once a frame and updates what changed.
Simple, works with any `UObject`, and costs a little work per bound field per frame.

You do not choose between these — **notification wins automatically when the source supports it**, and
a notifying source is never also polled. The `bPollEveryFrame` argument only affects sources *without*
notification: leave it `true` for anything that changes, or pass `false` for a screen whose values are
read once at bind time and never again, which saves the per-frame sweep entirely.

Folio logs which you got — look for `updating by notification` or `polled every frame`.

Both paths are covered by `Folio.ViewModel.*`, run with `./scripts/check_tests.sh`. The fixture there,
`Source/Folio/Private/Tests/FolioTestViewModel.h`, is also the smallest complete example of a C++ view
model: it implements `INotifyFieldValueChanged` by hand rather than deriving from `UMVVMViewModelBase`,
which is what a project avoiding the MVVM plugin needs to write.

### So should you use MVVM?

Often, no — and this is the opposite of the usual Unreal advice, so it is worth being clear about why.

In UMG, most of MVVM's value is the **editor-side** binding workflow: the bindings panel, compiled
bindings, the View extension. Folio uses none of that. What `UMVVMViewModelBase` gives a Folio project
is a `UObject` with change notification and nothing else, so the trade is very different from UMG,
where MVVM is close to a default choice.

There is also a layering point. **RmlUi's data model is already a view-model layer** — `data-model`
plus data expressions *is* the view/view-model split. Putting a `UMVVMViewModelBase` behind it can end
up as a view model feeding a view model.

**Reach for a view model when:**

- Values change irregularly and you would rather push than poll.
- The state is worth testing without a screen.
- The data should outlive, or not depend on, the document.
- You are already using MVVM elsewhere and consistency is worth more than the saving.

**Don't bother when:**

- The screen is static — a credits page, or settings read once on open. Notification buys nothing.
- The data changes every frame anyway. Notification can be *worse* here: the layout runs once per
  frame regardless, so a hundred broadcasts and one poll collapse into the same single update, and
  polling is the natural cadence. **A health bar during combat is exactly this case.**

For the health bar above, a plain `UObject` polled once a frame is the right answer. If it later grows
into a character sheet that changes on equip rather than continuously, adding `FieldNotify` is a
change to the source only — the document and the `BindViewModel` call stay as they are.

## Structuring it as things grow

- **One boundary object per screen, not per game system.** A `UFolioHudViewModel` that a HUD reads is
  easier to reason about than a document reaching into three subsystems.
- **Bind several models when a screen genuinely has several sources.** `BindViewModel` is repeatable
  with different names; markup selects with `data-model` per subtree.
- **Keep formatting in the markup.** `format()`, `data-class-*` and RCSS handle presentation. A
  `FString HealthText` on your view model is a smell: it moves a design decision into C++ where a
  designer cannot reach it.
- **Expose conditions, not appearance.** `bIsLow` or a comparison in the markup, never a colour.
- **Maps and sets cannot be bound.** Neither has an order a document could iterate, and RmlUi's data
  model has no shape for either. Folio warns and skips them. Expose a `TArray` instead - which does
  bind, and is the next section.

## Lists

A `TArray` binds like anything else, and `data-for` iterates it. A kill feed, a party list, an
inventory, a scoreboard - all the same shape:

```cpp
USTRUCT(BlueprintType)
struct FHudMate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "HUD") FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "HUD") float Health = 100.0f;
    UPROPERTY(BlueprintReadOnly, Category = "HUD") bool bTalking = false;
};

UPROPERTY(BlueprintReadOnly, Category = "HUD")
TArray<FHudMate> Party;
```

```html
<div class="mate" data-for="mate : Party" data-class-talking="mate.bTalking">
    <span class="name">{{ mate.Name }}</span>
    <div class="bar"><div class="fill" data-style-width="mate.Health + '%'"></div></div>
</div>
```

The element carrying `data-for` is the one repeated, and everything inside it addresses the alias.
`{{ Party.size }}` gives the length, which is the only way to reach it - a data expression cannot ask
an array how long it is otherwise, so "3 players" and empty-list messages both need it.

Three element types work:

| | |
|---|---|
| `TArray<FSomeStruct>` | Members by name, from the struct's `BlueprintReadOnly` / `EditAnywhere` properties. |
| `TArray<UObject*>` | The same, from the **declared** class. A derived element's extra properties are not visible, because a member name resolves against one shape. |
| `TArray<float>`, `TArray<FString>`, … | The element itself: `{{ tag }}`, no member. |

Arrays are read only. An element is mirrored, so a write would land on the copy - bind a command and
change the array in C++ instead.

**Changing the length is a change.** Appending, removing or emptying dirties the array, and `data-for`
rebuilds its children rather than reusing them against stale values. That also means a list that
churns every frame relays out every frame; a kill feed of five rows is nothing, five hundred
nameplates is not. See **[performance](performance_profiling.md)**.

Nothing needs registering per struct. The member list is resolved once at mount from the inner
property's type, which is what makes this work on a struct you have not told Folio about.

## If it does not work

- **`{{ braces }}` shown literally.** Nothing is bound to that model name, and no mock file was
  found. Check the name matches `data-model` exactly, and the log for `nothing is bound`.
- **A property is missing from the model.** It has no `BlueprintReadOnly`, `BlueprintReadWrite` or
  `EditAnywhere`. The log names it.
- **Values never change.** Check whether the log says notification or polling. A source that
  implements `INotifyFieldValueChanged` but never broadcasts will never update, and is not polled as a
  fallback — so the bug is a missing broadcast in the setter, not in the binding.
- **`72.000000` instead of `72`.** Add `| format(0)`.

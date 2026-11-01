# Showing game data: a health bar

A bar and a number, driven by a float on your character, and which binding approach to use.

Assumes you have finished [hello world](hello_world.md).

## The shape of it

A document never reaches into your game. Game code hands Folio **one object per screen**, and Folio exposes that object's properties to the markup as a *data model*:

```
your gameplay  →  a boundary object  →  data model  →  {{ Health }} in markup
```

Keep that boundary object small and about the screen, not the game: a health bar wants `Health` and `MaxHealth`, not a pointer to your character.

Which base class the object uses is a smaller decision, and you can change it later without touching the markup.

## 1. Add something to show

Put two floats on your character. Any `AActor` or `UObject` works:

```cpp
UPROPERTY(BlueprintReadOnly, Category = "Vitals")
float Health = 100.0f;

UPROPERTY(BlueprintReadOnly, Category = "Vitals")
float MaxHealth = 100.0f;
```

Folio only binds properties marked `BlueprintReadOnly`, `BlueprintReadWrite` or `EditAnywhere`; a plain `UPROPERTY()` stays invisible to markup.

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

`data-model="player"` names the model this subtree reads from. The rest does one of three jobs:

| | |
|---|---|
| `{{ Health \| format(0) }}` | Prints a value. `format(value, digits)` rounds a float for display; without it you get `72.000000`. |
| `data-style-width="..."` | Computes a style property. Arithmetic works, and the result needs units, so `+ '%'`. |
| `data-class-low="..."` | Adds or removes a class from a condition. Let RCSS decide what "low" *looks* like. |

Bind a **condition**, not a colour: the document decides appearance, and your C++ never mentions red. Write `&lt;` for less-than inside an XML attribute.

## 3. See it before writing any C++

Folio looks for `UI/mock/<model>.json` and mounts it if game code has not bound anything. Create `<YourProject>/UI/mock/player.json`:

```json
{ "Health": 72.0, "MaxHealth": 100.0 }
```

The preview shows a working bar:

```bash
./bin/win64/uipreview.exe <YourProject>/UI/healthbar.rml --root <YourProject>
```

Set `Health` to `18`, save, and the bar turns red with no code running. Mocks also show states that are awkward to reach in game.

## 4. Bind the real thing

Host the document with `UFolioWidget` and bind your object to the same model name:

```cpp
Document->BindViewModel(TEXT("player"), MyCharacter);
```

`Health` and `MaxHealth` are found by reflection, and the mock file steps aside once something is bound.

Bind before the document loads if you can, so the first frame does not show literal braces.

## Choosing how the data updates

Folio supports two ways, and the call site above is identical for both.

**Notified.** If your object implements `INotifyFieldValueChanged`, Folio subscribes per field and only touches the document when a value changes. `UMVVMViewModelBase` implements it.

**Polled.** Otherwise Folio re-reads every bound property once a frame and updates what changed. Works with any `UObject`, at a little cost per bound field per frame.

**Notification wins when the source supports it**, and a notifying source is never also polled. The `bPollEveryFrame` argument only affects sources *without* notification: leave it `true` for anything that changes, or pass `false` for a screen whose values are read once at bind time, which skips the per-frame sweep.

Folio logs which you got: look for `updating by notification` or `polled every frame`.

Both paths are covered by `Folio.ViewModel.*`, run with `./scripts/check_tests.sh`. Its fixture, `Source/Folio/Private/Tests/FolioTestViewModel.h`, is the smallest complete C++ view model: it implements `INotifyFieldValueChanged` by hand, which is what a project avoiding the MVVM plugin needs to write.

### So should you use MVVM?

Often, no, against the usual Unreal advice.

In UMG, most of MVVM's value is the **editor-side** binding workflow: the bindings panel, compiled bindings, the View extension. Folio uses none of that. What `UMVVMViewModelBase` gives a Folio project is a `UObject` with change notification and nothing else.

**RmlUi's data model is already a view-model layer**: `data-model` plus data expressions *is* the view/view-model split. A `UMVVMViewModelBase` behind it can end up as a view model feeding a view model.

**Reach for a view model when:**

- Values change irregularly and you would rather push than poll.
- The state is worth testing without a screen.
- The data should outlive, or not depend on, the document.
- You are already using MVVM elsewhere and consistency is worth more than the saving.

**Don't bother when:**

- The screen is static: a credits page, or settings read once on open. Notification buys nothing.
- The data changes every frame anyway. Notification can be *worse* here: the layout runs once per frame regardless, so a hundred broadcasts and one poll collapse into the same single update, and polling is the natural cadence. **A health bar during combat is exactly this case.**

For the health bar above, use a plain `UObject` polled once a frame. If it grows into a character sheet that changes on equip, adding `FieldNotify` changes the source only: the document and the `BindViewModel` call stay as they are.

## Structuring it as things grow

- **One boundary object per screen, not per game system.** A `UFolioHudViewModel` that a HUD reads is easier to reason about than a document reaching into three subsystems.
- **Bind several models when a screen has several sources.** `BindViewModel` is repeatable with different names; markup selects with `data-model` per subtree.
- **Keep formatting in the markup.** `format()`, `data-class-*` and RCSS handle presentation. A `FString HealthText` on your view model moves a design decision into C++ where a designer cannot reach it.
- **Expose conditions, not appearance.** `bIsLow` or a comparison in the markup, never a colour.
- **Maps and sets cannot be bound.** Neither has an order a document could iterate, and RmlUi's data model has no shape for either. Folio warns and skips them. Expose a `TArray` instead (next section).

## Lists

A `TArray` binds like anything else, and `data-for` iterates it. A kill feed, party list or scoreboard all take this shape:

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

The element carrying `data-for` is the one repeated, and everything inside it addresses the alias. `{{ Party.size }}` is the only way to read the length, so "3 players" and empty-list messages both need it.

Three element types work:

| | |
|---|---|
| `TArray<FSomeStruct>` | Members by name, from the struct's `BlueprintReadOnly` / `EditAnywhere` properties. |
| `TArray<UObject*>` | The same, from the **declared** class. A derived element's extra properties are not visible, because a member name resolves against one shape. |
| `TArray<float>`, `TArray<FString>`, … | The element itself: `{{ tag }}`, no member. |

Arrays are read only. An element is mirrored, so a write would land on the copy; bind a command and change the array in C++ instead.

**Changing the length is a change.** Appending, removing or emptying dirties the array, and `data-for` rebuilds its children rather than reusing them against stale values. That also means a list that churns every frame relays out every frame; a kill feed of five rows is nothing, five hundred nameplates is not. See **[performance](performance_profiling.md)**.

Nothing needs registering per struct: the member list is resolved once at mount from the inner property's type.

## If it does not work

- **`{{ braces }}` shown literally.** Nothing is bound to that model name, and no mock file was found. Check the name matches `data-model` exactly, and the log for `nothing is bound`.
- **A property is missing from the model.** It has no `BlueprintReadOnly`, `BlueprintReadWrite` or `EditAnywhere`. The log names it.
- **Values never change.** Check whether the log says notification or polling. A source that implements `INotifyFieldValueChanged` but never broadcasts never updates and is not polled as a fallback: the bug is a missing broadcast in the setter.
- **`72.000000` instead of `72`.** Add `| format(0)`.

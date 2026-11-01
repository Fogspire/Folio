# Generating a view model

A document already declares its binding surface: `data-model="hud"` names the model, `{{ Health }}` names a value, `data-event-click="Reload"` names a command. The matching C++ is generated from it.

```bash
./scripts/viewmodel.sh UI/screens/settings.rml \
    --out Source/MyGame/UI/Generated/SettingsViewModelBase.h --api MYGAME_API
```

Your module needs `ModelViewViewModel` and `FieldNotification` in its `Build.cs`, and the ModelViewViewModel plugin enabled.

## The two rules that matter

**A base class is generated, never the class you own.** `USettingsViewModelBase` is `Abstract` and holds the fields and the command stubs; `USettingsViewModel : USettingsViewModelBase` is yours and holds the logic. Regenerating the base is then always safe.

**Types come from the mock, names come from the markup.** `UI/mock/settings.json` already holds typed values, so `"Health": 0.75` is a float and `"bAlive": true` is a bool, with no type annotations in the markup.

That makes the two a cross-check:

- a name bound in the markup with no mock entry has no type, and is reported
- a mock entry nothing binds is reported too: expected when a mock is shared between documents, a typo in the markup otherwise

Run with `--strict` to make either an error, for a build step.

## Commands

A command takes its signature from the call site:

```html
<button data-event-click="Damage(0.25, 'a button')">Hit</button>
```

gives `void Damage(float InArg0, const FString& InArg1)` as a `BlueprintImplementableEvent`. A literal types itself; an argument that is a bound value types from the mock; anything else is reported rather than guessed.

## The loop

Generate, then restart the editor, with this in `DefaultEditorPerProjectUserSettings.ini`:

```ini
[/Script/UnrealEd.EditorLoadingSavingSettings]
bForceCompilationAtStartup=True
```

The generated file has to be on disk before the restart. A **failed** compile still opens the editor with the class absent, so the generator reports what it wrote, and Folio's binding bridge warns by name when a bound name resolves to nothing.

## From a schema instead

A mock cannot say a string is an `FText`, that a number is an enum, what a row of an array holds, or what a command's parameters are called, and it describes one document where a model often serves several. A schema can. [Folio Studio](studio.md) keeps one per model in `UI/schema/<model>.json`, and generates the mock from it:

```json
{
	"model": "pause",
	"class": "UPauseMenuModel",
	"category": "Menu",
	"documents": ["UI/screens/menu/pause.rml"],
	"structs": {
		"FMenuRow": { "fields": { "Label": { "type": "text" }, "bEnabled": { "type": "bool", "default": true }, "Fraction": { "type": "float" } } }
	},
	"fields": {
		"Rows": { "type": "array", "of": "FMenuRow", "sample": [{ "Label": "VOLUME", "bEnabled": true, "Fraction": 0.8 }] },
		"Title": { "type": "text", "sample": "AUDIO" },
		"Page": { "type": "enum", "enum": "EMenuPage", "sample": 2 },
		"bAlive": { "type": "bool", "sample": true }
	},
	"commands": { "Take": ["int Index"], "Step": ["int Index", "float Delta"] }
}
```

```bash
./scripts/viewmodel.sh --schema UI/schema/pause.json \
    --out Source/MyGame/UI/Generated/PauseMenuModelBase.h --api MYGAME_API
```

| | |
|---|---|
| Types | `bool`, `int`, `float`, `string`, `text`, `name`, `enum` (with `enum` naming it), `array` (with `of` naming a struct or a scalar type) |
| `sample` | What the mock holds, and so what a document previews with |
| `default` | The C++ initial value, when it should not be the type's zero |
| `writable` | `true` makes the property `BlueprintReadWrite`, for a `data-value` that writes back |
| `includes` | Headers the generated one needs, for an enum declared elsewhere |
| `--mock-out` | Also writes the mock the schema describes |
| `--strict` | Fails when the schema and the documents it names disagree |

This mode generates a plain `UObject` base rather than an MVVM one: public `UPROPERTY`s, a `ModelName()`, and one pure virtual `UFUNCTION` per command. Folio binds an object by reflection, so the properties are the binding. Derive from it, assign the fields, and pass an instance to `BindViewModel`:

```cpp
UCLASS()
class UPauseMenuModel : public UPauseMenuModelBase
{
	GENERATED_BODY()

public:
	virtual void Take(int32 Index) override;
	virtual void Step(int32 Index, float Delta) override;
};
```

```cpp
Model = NewObject<UPauseMenuModel>(this);
BindViewModel(UPauseMenuModel::ModelName(), Model);
```

A schema with `"source": "reflected"` was written by the Unreal editor from a class that already exists, through **Folio > Export View Model Schemas**. That class is the source of truth, and there is nothing to generate.

## What it does not do

It reads the document as text rather than through RmlUi, because `{{ }}` is rewritten into a data view as a document loads. So bindings introduced by a linked template are not seen; they surface as unused mock entries.

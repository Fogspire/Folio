# Generating a view model

A document already declares its binding surface: `data-model="hud"` names the model, `{{ Health }}`
names a value, `data-event-click="Reload"` names a command. Writing the matching C++ by hand is
transcription, so it is generated.

```bash
./scripts/viewmodel.sh UI/screens/settings.rml \
    --out Source/MyGame/UI/Generated/SettingsViewModelBase.h --api MYGAME_API
```

Your module needs `ModelViewViewModel` and `FieldNotification` in its `Build.cs`, and the
ModelViewViewModel plugin enabled.

## The two rules that matter

**A base class is generated, never the class you own.** `USettingsViewModelBase` is `Abstract` and
holds the fields and the command stubs; `USettingsViewModel : USettingsViewModelBase` is yours and
holds the logic. Regenerating the base is then always safe. Generating one file a person is expected
to then edit is the version that eventually overwrites somebody's work.

**Types come from the mock, names come from the markup.** `UI/mock/settings.json` already holds typed
values, because someone wrote it to see the screen before the game could fill it in. So `"Health":
0.75` is a float and `"bAlive": true` is a bool - no inference from expressions, and no type
annotations cluttering the markup.

That makes the two a cross-check, which is most of the value:

- a name bound in the markup with no mock entry has no type, and is reported
- a mock entry nothing binds is reported too - expected when a mock is shared between documents, a
  typo in the markup otherwise

Run with `--strict` to make either an error, which is what a build step wants.

## Commands

A command takes its signature from the call site:

```html
<button data-event-click="Damage(0.25, 'a button')">Hit</button>
```

gives `void Damage(float InArg0, const FString& InArg1)` as a `BlueprintImplementableEvent`. A literal
types itself; an argument that is a bound value types from the mock; anything else is reported rather
than guessed.

## The loop

Generate, then restart the editor. If the project sets this in
`DefaultEditorPerProjectUserSettings.ini`, that is the whole instruction:

```ini
[/Script/UnrealEd.EditorLoadingSavingSettings]
bForceCompilationAtStartup=True
```

Two things to watch. The generated file has to be on disk before the restart. And a **failed** compile
still opens the editor with the class simply absent, which reads as the tool having done nothing - so
the generator reports what it wrote, and Folio's binding bridge warns by name when a bound name
resolves to nothing.

## What it does not do

It reads the document as text rather than through RmlUi, because `{{ }}` is rewritten into a data view
as a document loads - a loaded document no longer knows what was written. The cost is that bindings
introduced by a linked template are not seen. Those surface as unused mock entries rather than
disappearing silently.

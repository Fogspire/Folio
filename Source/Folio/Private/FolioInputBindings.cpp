#include "FolioInputBindings.h"

#include "FolioInputGlyphs.h"
#include "FolioModule.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FolioInputBindings)

#define LOCTEXT_NAMESPACE "Folio"

/**
 * Takes the next key the player presses, ahead of everything else.
 *
 * A pre-processor rather than a focused widget, because the point is to catch keys nothing has
 * asked for - a gamepad face button, a mouse thumb button, a key the game has bound to something
 * that would otherwise fire while the player is choosing it.
 */
class FFolioKeyCapture final : public IInputProcessor
{
public:
	explicit FFolioKeyCapture(TFunction<void(const FKey&)> InOnKey) : OnKey(MoveTemp(InOnKey)) {}

	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
	{
		Take(Event.GetKey());
		return true;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
	{
		Take(Event.GetEffectingButton());
		return true;
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent& Event) override
	{
		// An axis at rest reports every frame. Only a real push is a choice.
		if (FMath::Abs(Event.GetAnalogValue()) > 0.6f)
		{
			Take(Event.GetKey());
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("FolioKeyCapture"); }

private:
	void Take(const FKey& Key)
	{
		if (OnKey && Key.IsValid())
		{
			// Copied first: the callback tears this processor down.
			TFunction<void(const FKey&)> Callback = OnKey;
			OnKey = nullptr;
			Callback(Key);
		}
	}

	TFunction<void(const FKey&)> OnKey;
};

namespace
{
	EPlayerMappableKeySlot SlotOf(const int32 Index)
	{
		return Index <= 0 ? EPlayerMappableKeySlot::First : EPlayerMappableKeySlot::Second;
	}

	void Describe(const FKey& Key, FString& OutText, FString& OutGlyph)
	{
		OutText = Key.IsValid() ? Folio::LabelFor(Key).ToString() : FString();
		const FName Glyph = Key.IsValid() ? Folio::GlyphFor(Key) : NAME_None;
		OutGlyph = Glyph.IsNone() ? FString() : Glyph.ToString();
	}
}

void UFolioInputBindings::Initialise(ULocalPlayer* InLocalPlayer)
{
	LocalPlayer = InLocalPlayer;
	Refresh();
}

UEnhancedInputUserSettings* UFolioInputBindings::GetSettings() const
{
	const UEnhancedInputLocalPlayerSubsystem* Subsystem =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

	return Subsystem ? Subsystem->GetUserSettings() : nullptr;
}

UEnhancedPlayerMappableKeyProfile* UFolioInputBindings::GetProfile() const
{
	const UEnhancedInputUserSettings* Settings = GetSettings();
	return Settings ? Settings->GetActiveKeyProfile() : nullptr;
}

void UFolioInputBindings::SetDevice(const bool bInGamepad)
{
	if (bGamepad != bInGamepad)
	{
		bGamepad = bInGamepad;
		Refresh();
	}
}

void UFolioInputBindings::Refresh()
{
	Bindings.Reset();

	const UEnhancedPlayerMappableKeyProfile* Profile = GetProfile();
	if (!Profile)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("no Enhanced Input key profile for this player, so there is nothing to rebind. The "
				"project needs player mappable keys on its input actions."));
		return;
	}

	for (const TPair<FName, FKeyMappingRow>& Row : Profile->GetPlayerMappingRows())
	{
		if (!Row.Value.HasAnyMappings())
		{
			continue;
		}

		FFolioKeyBinding Binding;
		Binding.Mapping = Row.Key.ToString();

		for (const FPlayerKeyMapping& Mapping : Row.Value.Mappings)
		{
			// Every mapping in a row is the same action in a different slot, so the naming is taken
			// from whichever comes first and the keys from each.
			if (Binding.Action.IsEmpty())
			{
				Binding.Action = Mapping.GetDisplayName().ToString();
				Binding.Category = Mapping.GetDisplayCategory().ToString();

				if (const UInputAction* Action = Mapping.GetAssociatedInputAction())
				{
					Binding.Description = Action->ActionDescription.ToString();
				}
			}

			Binding.bCustomised |= Mapping.IsCustomized();

			// Filled by device rather than by the slot the engine happens to have used, so a keyboard
			// page never shows a pad button and the columns mean what they say.
			const FKey& Key = Mapping.GetCurrentKey();
			if (!Key.IsValid() || Key.IsGamepadKey() != bGamepad)
			{
				continue;
			}

			if (Binding.PrimaryKey.IsEmpty())
			{
				Binding.PrimarySlot = static_cast<int32>(Mapping.GetSlot());
				Describe(Key, Binding.PrimaryKey, Binding.PrimaryGlyph);
			}
			else if (Binding.SecondaryKey.IsEmpty())
			{
				Binding.SecondarySlot = static_cast<int32>(Mapping.GetSlot());
				Describe(Key, Binding.SecondaryKey, Binding.SecondaryGlyph);
			}
		}

		if (Binding.Action.IsEmpty())
		{
			Binding.Action = Binding.Mapping;
		}

		Bindings.Add(MoveTemp(Binding));
	}

	// The game's order first, then category, then the name. Anything unlisted sorts after everything
	// listed rather than being scattered through it.
	const TArray<FName>& Wanted = Order;
	Bindings.Sort([&Wanted](const FFolioKeyBinding& A, const FFolioKeyBinding& B)
	{
		const int32 IndexA = Wanted.IndexOfByKey(FName(*A.Mapping));
		const int32 IndexB = Wanted.IndexOfByKey(FName(*B.Mapping));

		if (IndexA != IndexB)
		{
			return (IndexA == INDEX_NONE ? MAX_int32 : IndexA) < (IndexB == INDEX_NONE ? MAX_int32 : IndexB);
		}

		return A.Category == B.Category ? A.Action < B.Action : A.Category < B.Category;
	});
}

void UFolioInputBindings::Rebind(const FString& Mapping, const int32 Slot)
{
	if (bListening || Mapping.IsEmpty())
	{
		return;
	}

	Pending = FName(*Mapping);
	PendingSlot = Slot;
	PendingKey = FKey();

	const FFolioKeyBinding* Row = Bindings.FindByPredicate(
		[&Mapping](const FFolioKeyBinding& B) { return B.Mapping == Mapping; });

	Listening = Row ? Row->Action : Mapping;
	bListening = true;
	bConflict = false;
	ConflictWith.Reset();
	ConflictKey.Reset();

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	Capture = MakeShared<FFolioKeyCapture>([this](const FKey& Key) { OnKeyCaptured(Key); });

	// Ahead of the game and ahead of the UI: the whole point is to see a key nothing else lets
	// through.
	FSlateApplication::Get().RegisterInputPreProcessor(Capture, EInputPreProcessorType::PreGame);
}

void UFolioInputBindings::OnKeyCaptured(const FKey& Key)
{
	StopListening();

	if (Key == EKeys::Escape)
	{
		return;
	}

	const UEnhancedPlayerMappableKeyProfile* Profile = GetProfile();
	if (!Profile)
	{
		return;
	}

	TArray<FName> Mapped;
	Profile->GetMappingNamesForKey(Key, Mapped);
	Mapped.Remove(Pending);

	if (Mapped.IsEmpty())
	{
		Apply(Key);
		return;
	}

	// Named rather than counted, because "already bound" is useless without saying to what.
	TArray<FString> Names;
	Names.Reserve(Mapped.Num());
	for (const FName& Name : Mapped)
	{
		const FFolioKeyBinding* Row = Bindings.FindByPredicate(
			[&Name](const FFolioKeyBinding& B) { return B.Mapping == Name.ToString(); });

		Names.Add(Row ? Row->Action : Name.ToString());
	}

	PendingKey = Key;
	ConflictKey = Folio::LabelFor(Key).ToString();
	ConflictWith = FString::Join(Names, TEXT(", "));
	bConflict = true;
}

void UFolioInputBindings::ConfirmConflict()
{
	if (!bConflict)
	{
		return;
	}

	bConflict = false;
	Apply(PendingKey);
}

void UFolioInputBindings::CancelRebind()
{
	StopListening();
	bConflict = false;
	ConflictWith.Reset();
	ConflictKey.Reset();
	PendingKey = FKey();
}

void UFolioInputBindings::StopListening()
{
	bListening = false;
	Listening.Reset();

	if (Capture.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(Capture);
		}
		Capture.Reset();
	}
}

void UFolioInputBindings::Apply(const FKey& Key)
{
	UEnhancedInputUserSettings* Settings = GetSettings();
	if (!Settings || Pending.IsNone() || !Key.IsValid())
	{
		return;
	}

	FMapPlayerKeyArgs Args;
	Args.MappingName = Pending;
	Args.Slot = SlotOf(PendingSlot);
	Args.NewKey = Key;
	Args.bCreateMatchingSlotIfNeeded = true;

	FGameplayTagContainer Failure;
	Settings->MapPlayerKey(Args, Failure);

	if (!Failure.IsEmpty())
	{
		UE_LOG(LogFolio, Warning, TEXT("could not bind %s to '%s': %s"),
			*Key.ToString(), *Pending.ToString(), *Failure.ToString());
	}

	Settings->ApplySettings();
	Settings->SaveSettings();

	Refresh();
}

void UFolioInputBindings::ClearBinding(const FString& Mapping)
{
	UEnhancedInputUserSettings* Settings = GetSettings();
	if (!Settings || Mapping.IsEmpty())
	{
		return;
	}

	FGameplayTagContainer Failure;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = FName(*Mapping);
		Args.Slot = SlotOf(Slot);
		Settings->UnMapPlayerKey(Args, Failure);
	}

	Settings->ApplySettings();
	Settings->SaveSettings();

	Refresh();
}

void UFolioInputBindings::ClearSlot(const FString& Mapping, const int32 Slot)
{
	UEnhancedInputUserSettings* Settings = GetSettings();
	if (!Settings || Mapping.IsEmpty())
	{
		return;
	}

	FMapPlayerKeyArgs Args;
	Args.MappingName = FName(*Mapping);
	Args.Slot = SlotOf(Slot);

	FGameplayTagContainer Failure;
	Settings->UnMapPlayerKey(Args, Failure);

	Settings->ApplySettings();
	Settings->SaveSettings();

	Refresh();
}

void UFolioInputBindings::ResetBinding(const FString& Mapping)
{
	UEnhancedInputUserSettings* Settings = GetSettings();
	if (!Settings || Mapping.IsEmpty())
	{
		return;
	}

	FMapPlayerKeyArgs Args;
	Args.MappingName = FName(*Mapping);

	FGameplayTagContainer Failure;
	Settings->ResetAllPlayerKeysInRow(Args, Failure);

	Settings->ApplySettings();
	Settings->SaveSettings();

	Refresh();
}

void UFolioInputBindings::SetActionOrder(const TArray<FName>& MappingNames)
{
	Order = MappingNames;
	Refresh();
}

void UFolioInputBindings::ResetAll()
{
	// Only what this page is showing. The list is already filtered to the active device, so every row
	// in it has a key of that device on it; a row with nothing on this device is not touched.
	for (const FFolioKeyBinding& Binding : TArray<FFolioKeyBinding>(Bindings))
	{
		if (!Binding.PrimaryKey.IsEmpty() || !Binding.SecondaryKey.IsEmpty())
		{
			ResetBinding(Binding.Mapping);
		}
	}
}

void UFolioInputBindings::BeginDestroy()
{
	StopListening();
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE

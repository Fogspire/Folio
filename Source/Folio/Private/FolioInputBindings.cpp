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
	FFolioKeyCapture(TFunction<void(const FKey&)> InOnKey, TFunction<bool(const FKey&)> InAccepts)
		: OnKey(MoveTemp(InOnKey)), Accepts(MoveTemp(InAccepts)) {}

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
		if (FMath::Abs(Event.GetAnalogValue()) > 0.6f && Accepts(Event.GetKey()))
		{
			Take(Event.GetKey());
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("FolioKeyCapture"); }

private:
	/** A key Accepts turns down is still swallowed, so it neither binds nor reaches the game. */
	void Take(const FKey& Key)
	{
		if (OnKey && Key.IsValid() && Accepts(Key))
		{
			// Copied first: the callback tears this processor down.
			TFunction<void(const FKey&)> Callback = OnKey;
			OnKey = nullptr;
			Callback(Key);
		}
	}

	TFunction<void(const FKey&)> OnKey;
	TFunction<bool(const FKey&)> Accepts;
};

namespace
{
	EPlayerMappableKeySlot SlotOf(const int32 Index)
	{
		return static_cast<EPlayerMappableKeySlot>(FMath::Clamp(Index, 0, static_cast<int32>(EPlayerMappableKeySlot::Seventh)));
	}

	/** Escape, or the pad's view button for a player with no keyboard in reach. Never a binding. */
	bool IsCancelKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Left;
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

bool UFolioInputBindings::IsOnDevice(const FPlayerKeyMapping& Mapping) const
{
	// The default says which device a mapping is for, so one the player cleared still has its place.
	const FKey& Key = Mapping.GetDefaultKey().IsValid() ? Mapping.GetDefaultKey() : Mapping.GetCurrentKey();
	return Key.IsValid() && Key.IsGamepadKey() == bGamepad;
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

		TArray<const FPlayerKeyMapping*> Sorted;
		for (const FPlayerKeyMapping& Mapping : Row.Value.Mappings)
		{
			Sorted.Add(&Mapping);
		}
		Sorted.Sort([](const FPlayerKeyMapping& A, const FPlayerKeyMapping& B) { return A.GetSlot() < B.GetSlot(); });

		bool bPrimary = false;
		bool bSecondary = false;
		TSet<int32> Taken;
		for (const FPlayerKeyMapping* Each : Sorted)
		{
			const FPlayerKeyMapping& Mapping = *Each;
			Taken.Add(static_cast<int32>(Mapping.GetSlot()));

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

			// Filled by device rather than by the slot the engine happens to have used, so a keyboard
			// page never shows a pad button and the columns mean what they say.
			if (!IsOnDevice(Mapping))
			{
				continue;
			}

			Binding.bCustomised |= Mapping.IsCustomized();

			if (!bPrimary)
			{
				bPrimary = true;
				Binding.PrimarySlot = static_cast<int32>(Mapping.GetSlot());
				Describe(Mapping.GetCurrentKey(), Binding.PrimaryKey, Binding.PrimaryGlyph);
			}
			else if (!bSecondary)
			{
				bSecondary = true;
				Binding.SecondarySlot = static_cast<int32>(Mapping.GetSlot());
				Describe(Mapping.GetCurrentKey(), Binding.SecondaryKey, Binding.SecondaryGlyph);
			}
		}

		// A column with nothing behind it writes to a slot no other device holds, or binding a pad
		// button there would replace the keyboard key sharing the row.
		auto FreeSlot = [&Taken]()
		{
			int32 Slot = 0;
			while (Taken.Contains(Slot))
			{
				++Slot;
			}
			Taken.Add(Slot);
			return Slot;
		};
		if (!bPrimary)
		{
			Binding.PrimarySlot = FreeSlot();
		}
		if (!bSecondary)
		{
			Binding.SecondarySlot = FreeSlot();
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

	// A rebinding screen with nothing on it looks like markup that failed rather than a profile with
	// nothing in it, and the two are fixed in different places.
	if (Bindings.IsEmpty())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("the active key profile lists no mappable actions, so a rebinding screen built from it "
				"is blank. Enhanced Input lists an action once its mapping context has been registered "
				"with this player's user settings and its keys carry Player Mappable Key Settings - a "
				"context added to the subsystem alone is not registered."));
	}
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

	RebindEventDelegate.Broadcast(Folio::ERebindEvent::Listening);

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	Capture = MakeShared<FFolioKeyCapture>(
		[this](const FKey& Key) { OnKeyCaptured(Key); },
		[this](const FKey& Key) { return IsCancelKey(Key) || Key.IsGamepadKey() == bGamepad; });

	// Ahead of the game and ahead of the UI: the whole point is to see a key nothing else lets
	// through.
	FSlateApplication::Get().RegisterInputPreProcessor(Capture, EInputPreProcessorType::PreGame);
}

void UFolioInputBindings::OnKeyCaptured(const FKey& Key)
{
	StopListening();

	if (IsCancelKey(Key))
	{
		RebindEventDelegate.Broadcast(Folio::ERebindEvent::Cancelled);
		return;
	}

	const UEnhancedPlayerMappableKeyProfile* Profile = GetProfile();
	if (!Profile)
	{
		RebindEventDelegate.Broadcast(Folio::ERebindEvent::Cancelled);
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

	RebindEventDelegate.Broadcast(Folio::ERebindEvent::Conflict);
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

	RebindEventDelegate.Broadcast(Folio::ERebindEvent::Cancelled);
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

	RebindEventDelegate.Broadcast(Folio::ERebindEvent::Captured);
}

void UFolioInputBindings::ClearBinding(const FString& Mapping)
{
	UEnhancedInputUserSettings* Settings = GetSettings();
	if (!Settings || Mapping.IsEmpty())
	{
		return;
	}

	const FFolioKeyBinding* Row = Bindings.FindByPredicate(
		[&Mapping](const FFolioKeyBinding& B) { return B.Mapping == Mapping; });
	if (!Row)
	{
		return;
	}

	FGameplayTagContainer Failure;
	for (const int32 Slot : { Row->PrimarySlot, Row->SecondarySlot })
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = FName(*Mapping);
		Args.Slot = SlotOf(Slot);
		Settings->UnMapPlayerKey(Args, Failure);
	}

	Settings->ApplySettings();
	Settings->SaveSettings();

	Refresh();

	RebindEventDelegate.Broadcast(Folio::ERebindEvent::Cleared);
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

	RebindEventDelegate.Broadcast(Folio::ERebindEvent::Cleared);
}

void UFolioInputBindings::ResetBinding(const FString& Mapping)
{
	UEnhancedInputUserSettings* Settings = GetSettings();
	if (!Settings || Mapping.IsEmpty())
	{
		return;
	}

	const UEnhancedPlayerMappableKeyProfile* Profile = GetProfile();
	const FKeyMappingRow* Row = Profile ? Profile->FindKeyMappingRow(FName(*Mapping)) : nullptr;
	if (!Row)
	{
		return;
	}

	// This device's keys only: a reset on the gamepad page leaves the keyboard as the player set it.
	TArray<FMapPlayerKeyArgs> Resets;
	for (const FPlayerKeyMapping& Each : Row->Mappings)
	{
		if (IsOnDevice(Each) && Each.IsCustomized())
		{
			FMapPlayerKeyArgs& Args = Resets.AddDefaulted_GetRef();
			Args.MappingName = FName(*Mapping);
			Args.Slot = Each.GetSlot();
			Args.NewKey = Each.GetDefaultKey();
		}
	}

	FGameplayTagContainer Failure;
	for (const FMapPlayerKeyArgs& Args : Resets)
	{
		if (Args.NewKey.IsValid())
		{
			Settings->MapPlayerKey(Args, Failure);
		}
		else
		{
			Settings->UnMapPlayerKey(Args, Failure);
		}
	}

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
	// Only what this page is showing: ResetBinding touches this device's keys and nothing else.
	for (const FFolioKeyBinding& Binding : TArray<FFolioKeyBinding>(Bindings))
	{
		if (Binding.bCustomised)
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

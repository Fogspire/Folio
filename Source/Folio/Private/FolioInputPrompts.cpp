#include "FolioInputPrompts.h"

#include "FolioInputGlyphs.h"
#include "FolioModule.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FolioInputPrompts)

namespace
{
	/**
	 * The key a prompt should show for an action.
	 *
	 * Filtered by what the player is holding, because a prompt saying `F` while they are on a pad is
	 * worse than no prompt: they will go looking for a key that does nothing.
	 */
	FKey FindKey(const ULocalPlayer* LocalPlayer, const FName MappingName, const bool bGamepad)
	{
		const UEnhancedInputLocalPlayerSubsystem* Subsystem =
			LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;

		const UEnhancedInputUserSettings* Settings = Subsystem ? Subsystem->GetUserSettings() : nullptr;
		const UEnhancedPlayerMappableKeyProfile* Profile = Settings ? Settings->GetActiveKeyProfile() : nullptr;

		const FKeyMappingRow* Row = Profile ? Profile->FindKeyMappingRow(MappingName) : nullptr;
		if (!Row)
		{
			return FKey();
		}

		// A row holds every slot, and the slots are not sorted by device, so the first key of the
		// right kind wins rather than the first key.
		FKey Fallback;
		for (const FPlayerKeyMapping& Mapping : Row->Mappings)
		{
			const FKey& Key = Mapping.GetCurrentKey();
			if (!Key.IsValid())
			{
				continue;
			}

			if (Key.IsGamepadKey() == bGamepad)
			{
				return Key;
			}

			if (!Fallback.IsValid())
			{
				Fallback = Key;
			}
		}

		return Fallback;
	}
}

bool UFolioPromptSet::IsGamepadActive()
{
	return FFolioModule::IsAvailable() && FFolioModule::Get().GetInputDevice() == Folio::EInputDevice::Gamepad;
}

void UFolioPromptSet::SetGamepadActive(const bool bGamepad)
{
	if (FFolioModule::IsAvailable())
	{
		FFolioModule::Get().SetInputDevice(bGamepad ? Folio::EInputDevice::Gamepad : Folio::EInputDevice::Keyboard);
	}
}

void UFolioPromptSet::Add(const FName MappingName, const FString& Label)
{
	Wanted.Emplace(MappingName, Label);
}

void UFolioPromptSet::Clear()
{
	Wanted.Reset();
	Prompts.Reset();
}

void UFolioPromptSet::Refresh(ULocalPlayer* InLocalPlayer)
{
	const bool bGamepad = FFolioModule::IsAvailable()
		&& FFolioModule::Get().GetInputDevice() == Folio::EInputDevice::Gamepad;

	Prompts.Reset(Wanted.Num());

	for (const TPair<FName, FString>& Each : Wanted)
	{
		const FKey Key = FindKey(InLocalPlayer, Each.Key, bGamepad);

		FFolioPrompt Prompt;
		Prompt.Label = Each.Value;
		Prompt.bBound = Key.IsValid();

		if (Prompt.bBound)
		{
			Prompt.Glyph = Folio::ResolveGlyph(Key, InLocalPlayer);
		}

		Prompts.Add(MoveTemp(Prompt));
	}
}

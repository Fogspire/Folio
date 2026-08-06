#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"

#include "FolioInputBindings.generated.h"

class ULocalPlayer;
class UEnhancedInputUserSettings;
class UEnhancedPlayerMappableKeyProfile;

namespace Folio
{
	/** What the rebinding flow just did, for a host that wants to sound it. */
	enum class ERebindEvent : uint8
	{
		/** Waiting for a key. */
		Listening,
		/** A key was taken and written. */
		Captured,
		/** The key pressed is already spoken for, and the player has to agree. */
		Conflict,
		/** The wait ended with nothing written. */
		Cancelled,
		/** An action was left with no key on it. */
		Cleared,
		/** An action, or the whole list, went back to what it shipped as. */
		Reset,
	};
}

/** One rebindable action, as a document reads it. */
USTRUCT(BlueprintType)
struct FFolioKeyBinding
{
	GENERATED_BODY()

	/** What Enhanced Input calls it. The commands take this back. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Mapping;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Action;

	/** The action's group, for a list that wants headings. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Category;

	/** The Input Action asset's own description, if the person who made it wrote one. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString PrimaryKey;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString PrimaryGlyph;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString SecondaryKey;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString SecondaryGlyph;

	/** Which engine slot each column came from, so a rebind writes back to the right one. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	int32 PrimarySlot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	int32 SecondarySlot = 1;

	/** The player changed this one, so a reset is worth offering. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	bool bCustomised = false;
};

/**
 * The rebinding screen's model: every mappable action, and the commands to change one.
 *
 * Bind it as a data model and the whole screen is markup - `data-for` over `Bindings`, and
 * `data-event-click="Rebind(mapping, 0)"` on the key. Nothing here draws anything or assumes a
 * layout, so a game's own list looks like the game rather than like this plugin.
 *
 * The source of truth is Enhanced Input's own `UEnhancedInputUserSettings`, so a mapping changed
 * here is the mapping every other system already reads, saved through the same path, with no
 * parallel table to keep in step.
 */
UCLASS(BlueprintType)
class FOLIO_API UFolioInputBindings : public UObject
{
	GENERATED_BODY()

public:
	/** Reads the player's current profile. Repeatable: call it again when the profile changes. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Initialise(ULocalPlayer* InLocalPlayer);

	/**
	 * Which device the list is for.
	 *
	 * A row holds every key bound to an action regardless of what it is, so without this a keyboard
	 * page lists a gamepad button as its secondary binding - which is not something a player editing
	 * their keyboard can act on, and not something they can see the real secondary behind.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void SetDevice(bool bInGamepad);

	/** True while the list is showing gamepad bindings. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	bool bGamepad = false;

	/**
	 * Waits for a key and puts it in a slot.
	 *
	 * Slot 0 is the primary binding and 1 the secondary. While waiting, `bListening` is true and
	 * `Listening` names the action, which is all a "press any key" panel needs. Escape cancels.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Rebind(const FString& Mapping, int32 Slot);

	UFUNCTION(BlueprintCallable, Category = "Folio")
	void CancelRebind();

	/** Applies a key the player was warned already had an action on it. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void ConfirmConflict();

	UFUNCTION(BlueprintCallable, Category = "Folio")
	void ClearBinding(const FString& Mapping);

	/** Clears one column. What a right click on a key is for. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void ClearSlot(const FString& Mapping, int32 Slot);

	UFUNCTION(BlueprintCallable, Category = "Folio")
	void ResetBinding(const FString& Mapping);

	/** Resets every action that has a key on the device the list is showing. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void ResetAll();

	/**
	 * The order actions should appear in, by mapping name.
	 *
	 * Enhanced Input has no notion of one action mattering more than another, so alphabetical is all
	 * there is to fall back on - and alphabetical puts "Move Backward" above "Move Forward", which
	 * reads as a bug. Anything not named here follows in the usual order.
	 */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void SetActionOrder(const TArray<FName>& MappingNames);

	/** Every mappable action in the player's current profile. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	TArray<FFolioKeyBinding> Bindings;

	/** True while a key is being waited for. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	bool bListening = false;

	/** The action being rebound, for the panel to name. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString Listening;

	/** True when the key pressed already has actions on it and the player has to agree. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	bool bConflict = false;

	/** What the key is already bound to, and the key, for the warning to read out. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString ConflictWith;

	UPROPERTY(BlueprintReadOnly, Category = "Folio")
	FString ConflictKey;

	/**
	 * Every step of the flow as it happens.
	 *
	 * The state above says where the flow is, which a document reads once a frame and is all a panel
	 * needs. A sound needs the moment rather than the state, and there is nowhere else to take it from:
	 * the document calls the commands directly.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRebindEvent, Folio::ERebindEvent);
	FOnRebindEvent& OnRebindEvent() { return RebindEventDelegate; }

	virtual void BeginDestroy() override;

private:
	void Refresh();
	void Apply(const FKey& Key);
	void StopListening();

	/** Called by the capture with whatever the player pressed. */
	void OnKeyCaptured(const FKey& Key);

	UEnhancedInputUserSettings* GetSettings() const;
	UEnhancedPlayerMappableKeyProfile* GetProfile() const;

	UPROPERTY(Transient)
	TObjectPtr<ULocalPlayer> LocalPlayer;

	TArray<FName> Order;

	FName Pending;
	int32 PendingSlot = 0;
	FKey PendingKey;

	FOnRebindEvent RebindEventDelegate;

	TSharedPtr<class FFolioKeyCapture> Capture;
};

#pragma once

#include "CoreMinimal.h"
#include "FieldNotificationDeclaration.h"
#include "FieldNotificationDelegate.h"
#include "INotifyFieldValueChanged.h"
#include "UObject/Object.h"

#include "FolioBindTest.generated.h"

/**
 * A source for proving the view model bridge, without needing the MVVM plugin.
 *
 * It implements INotifyFieldValueChanged the same way UMVVMViewModelBase does - that interface and
 * its declaration macros are the engine's own FieldNotification module, not the ModelViewViewModel
 * plugin. So this exercises the exact path a real view model takes through the bridge, in a test
 * that builds in any project.
 *
 * That is also why the bridge does not link MVVM: the interface is the seam, and a view model is
 * simply one thing that satisfies it.
 */
UCLASS()
class UFolioBindTest : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Folio", meta = (AllowPrivateAccess = true))
	int32 Ticks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Folio", meta = (AllowPrivateAccess = true))
	float Health = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Folio", meta = (AllowPrivateAccess = true))
	bool bAlive = true;

	UPROPERTY(BlueprintReadOnly, Category = "Folio", meta = (AllowPrivateAccess = true))
	FString Status = TEXT("starting");

	/** Whether Advance keeps moving Health on its own. Off makes the commands legible. */
	UPROPERTY(BlueprintReadOnly, Category = "Folio", meta = (AllowPrivateAccess = true))
	bool bDrifting = true;

	//
	// Writable, so a control can move them. Read-write is what a settings value looks like; everything
	// above is BlueprintReadOnly and must stay unwritable, which is half of what the test proves.
	//

	UPROPERTY(BlueprintReadWrite, Category = "Folio", meta = (AllowPrivateAccess = true))
	int32 Volume = 50;

	UPROPERTY(BlueprintReadWrite, Category = "Folio", meta = (AllowPrivateAccess = true))
	float Gamma = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Folio", meta = (AllowPrivateAccess = true))
	bool bSubtitles = false;

	UPROPERTY(BlueprintReadWrite, Category = "Folio", meta = (AllowPrivateAccess = true))
	FString PlayerName = TEXT("Player");

	/** Not marked visible, so the bridge must leave it out of the model. */
	UPROPERTY()
	int32 Hidden = 99;

	/** Advances the values and reports each one that moved, so a bound document updates. */
	void Advance();

	/** How many notifications have been broadcast, so a test can prove they happened. */
	int32 GetBroadcastCount() const { return BroadcastCount; }

	/** Stops or starts the automatic drift, so a command's effect on Health can be seen at all. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void ToggleDrift();

	/** Reachable from markup as data-event-click="Revive". Proves the no-argument case. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Revive();

	/** Reachable as data-event-click="Damage(0.25)". Proves arguments arrive in order and typed. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	void Damage(float Amount, const FString& Cause);

	/** Rejected by the bridge: a command cannot return anything. Its absence is the assertion. */
	UFUNCTION(BlueprintCallable, Category = "Folio")
	int32 CountCommands() const { return CommandCount; }

	/** How many times markup has called in, so a test can prove the callback fired. */
	int32 GetCommandCount() const { return CommandCount; }

	UE_FIELD_NOTIFICATION_DECLARE_CLASS_DESCRIPTOR_BASE_BEGIN(FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Ticks, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Health, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(bAlive, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Status, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(bDrifting, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD_BEGIN(Ticks)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(Health)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(bAlive)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(Status)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(bDrifting)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD_END()
	UE_FIELD_NOTIFICATION_DECLARE_CLASS_DESCRIPTOR_BASE_END()

	//~ Begin INotifyFieldValueChanged
	virtual FDelegateHandle AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId,
		FFieldValueChangedDelegate InNewDelegate) override;
	virtual bool RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId,
		FDelegateHandle InHandle) override;
	virtual int32 RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject) override;
	virtual int32 RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId,
		FDelegateUserObjectConst InUserObject) override;
	virtual void BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId) override;
	//~ End INotifyFieldValueChanged

private:
	UE::FieldNotification::FFieldMulticastDelegate Delegates;
	TBitArray<> EnabledFields;
	int32 BroadcastCount = 0;
	int32 CommandCount = 0;
};

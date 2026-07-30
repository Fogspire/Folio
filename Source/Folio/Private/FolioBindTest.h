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

	/** Not marked visible, so the bridge must leave it out of the model. */
	UPROPERTY()
	int32 Hidden = 99;

	/** Advances the values and reports each one that moved, so a bound document updates. */
	void Advance();

	/** How many notifications have been broadcast, so a test can prove they happened. */
	int32 GetBroadcastCount() const { return BroadcastCount; }

	UE_FIELD_NOTIFICATION_DECLARE_CLASS_DESCRIPTOR_BASE_BEGIN(FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Ticks, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Health, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(bAlive, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Status, FOLIO_API)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD_BEGIN(Ticks)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(Health)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(bAlive)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(Status)
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
};

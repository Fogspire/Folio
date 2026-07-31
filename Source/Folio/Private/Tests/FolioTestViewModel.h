#pragma once

#include "CoreMinimal.h"
#include "FieldNotificationDeclaration.h"
#include "FieldNotificationDelegate.h"
#include "INotifyFieldValueChanged.h"
#include "UObject/Object.h"

#include "FolioTestViewModel.generated.h"

/**
 * A view model that reports its own changes, for the tests to bind against.
 *
 * Written out by hand rather than derived from `UMVVMViewModelBase`, because that lives in the
 * ModelViewViewModel plugin and the whole claim being tested is that Folio needs only the engine's
 * `FieldNotification` interface. Deriving from MVVM would test MVVM.
 *
 * It is also the smallest complete example of what a C++ view model has to provide, which is worth
 * having somewhere readable.
 */
UCLASS(NotBlueprintable, Hidden)
class UFolioTestViewModel : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Folio")
	int32 Score = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Folio")
	FString PlayerName;

	/** Writes and announces, which is the whole contract of a view model. */
	void SetScore(int32 NewScore);
	void SetPlayerName(const FString& NewName);

	/** Writes without announcing, so a test can tell notification apart from polling. */
	void SetScoreSilently(const int32 NewScore) { Score = NewScore; }

	UE_FIELD_NOTIFICATION_DECLARE_CLASS_DESCRIPTOR_BASE_BEGIN()
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD_BEGIN(Score)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD(PlayerName)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD_END()
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(Score, )
		UE_FIELD_NOTIFICATION_DECLARE_FIELD(PlayerName, )
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
	UE::FieldNotification::FFieldMulticastDelegate Notifications;
};

/**
 * The same two fields on an object that announces nothing.
 *
 * Any UObject is bindable, and most of what a game already has - a game state, a settings object -
 * is this rather than a view model. Polling is the only way its changes are seen.
 */
UCLASS(NotBlueprintable, Hidden)
class UFolioTestPlainModel : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Folio")
	int32 Score = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Folio")
	FString PlayerName;
};

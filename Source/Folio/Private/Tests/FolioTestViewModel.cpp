#include "FolioTestViewModel.h"

// The trailing semicolon is not optional: the macro closes an array initialiser, not a statement.
UE_FIELD_NOTIFICATION_IMPLEMENT_CLASS_DESCRIPTOR_TwoFields(UFolioTestViewModel, Score, PlayerName);

void UFolioTestViewModel::FFieldNotificationClassDescriptor::ForEachField(const UClass* Class,
	TFunctionRef<bool(::UE::FieldNotification::FFieldId)> Callback) const
{
	for (const UE::FieldNotification::FFieldId* Field : AllFields)
	{
		if (!Callback(*Field))
		{
			return;
		}
	}
}

void UFolioTestViewModel::SetScore(const int32 NewScore)
{
	if (Score == NewScore)
	{
		return;
	}

	Score = NewScore;
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Score);
}

void UFolioTestViewModel::SetPlayerName(const FString& NewName)
{
	if (PlayerName == NewName)
	{
		return;
	}

	PlayerName = NewName;
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::PlayerName);
}

FDelegateHandle UFolioTestViewModel::AddFieldValueChangedDelegate(
	const UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate)
{
	return InFieldId.IsValid()
		? Notifications.Add(this, InFieldId, MoveTemp(InNewDelegate))
		: FDelegateHandle();
}

bool UFolioTestViewModel::RemoveFieldValueChangedDelegate(
	const UE::FieldNotification::FFieldId InFieldId, const FDelegateHandle InHandle)
{
	return InFieldId.IsValid() && InHandle.IsValid()
		&& Notifications.RemoveFrom(this, InFieldId, InHandle).bRemoved;
}

int32 UFolioTestViewModel::RemoveAllFieldValueChangedDelegates(const FDelegateUserObjectConst InUserObject)
{
	return Notifications.RemoveAll(this, InUserObject).RemoveCount;
}

int32 UFolioTestViewModel::RemoveAllFieldValueChangedDelegates(
	const UE::FieldNotification::FFieldId InFieldId, const FDelegateUserObjectConst InUserObject)
{
	return Notifications.RemoveAll(this, InFieldId, InUserObject).RemoveCount;
}

void UFolioTestViewModel::BroadcastFieldValueChanged(const UE::FieldNotification::FFieldId InFieldId)
{
	Notifications.Broadcast(this, InFieldId);
}

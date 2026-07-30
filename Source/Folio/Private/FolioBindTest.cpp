#include "FolioBindTest.h"

UE_FIELD_NOTIFICATION_IMPLEMENT_CLASS_DESCRIPTOR_FourFields(UFolioBindTest, Ticks, Health, bAlive, Status);

// The BASE descriptor declares this but leaves it to the class, since there is no parent descriptor
// to chain into.
void UFolioBindTest::FFieldNotificationClassDescriptor::ForEachField(const UClass*,
	TFunctionRef<bool(UE::FieldNotification::FFieldId)> Callback) const
{
	for (int32 Index = 0; Index < Max_IndexOf_; ++Index)
	{
		if (!Callback(*AllFields[Index]))
		{
			return;
		}
	}
}

FDelegateHandle UFolioBindTest::AddFieldValueChangedDelegate(
	const UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate)
{
	if (!InFieldId.IsValid())
	{
		return FDelegateHandle();
	}

	const FDelegateHandle Handle = Delegates.Add(this, InFieldId, MoveTemp(InNewDelegate));
	if (Handle.IsValid())
	{
		EnabledFields.PadToNum(InFieldId.GetIndex() + 1, false);
		EnabledFields[InFieldId.GetIndex()] = true;
	}
	return Handle;
}

bool UFolioBindTest::RemoveFieldValueChangedDelegate(const UE::FieldNotification::FFieldId InFieldId,
	const FDelegateHandle InHandle)
{
	if (!InFieldId.IsValid() || !InHandle.IsValid())
	{
		return false;
	}

	const UE::FieldNotification::FFieldMulticastDelegate::FRemoveFromResult Result =
		Delegates.RemoveFrom(this, InFieldId, InHandle);

	if (Result.bRemoved && !Result.bHasOtherBoundDelegates)
	{
		EnabledFields[InFieldId.GetIndex()] = false;
	}

	return Result.bRemoved;
}

int32 UFolioBindTest::RemoveAllFieldValueChangedDelegates(const FDelegateUserObjectConst InUserObject)
{
	const UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult Result =
		Delegates.RemoveAll(this, InUserObject);

	EnabledFields = Result.HasFields;
	return Result.RemoveCount;
}

int32 UFolioBindTest::RemoveAllFieldValueChangedDelegates(const UE::FieldNotification::FFieldId InFieldId,
	const FDelegateUserObjectConst InUserObject)
{
	const UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult Result =
		Delegates.RemoveAll(this, InFieldId, InUserObject);

	EnabledFields = Result.HasFields;
	return Result.RemoveCount;
}

void UFolioBindTest::BroadcastFieldValueChanged(const UE::FieldNotification::FFieldId InFieldId)
{
	if (!InFieldId.IsValid() || !EnabledFields.IsValidIndex(InFieldId.GetIndex())
		|| !EnabledFields[InFieldId.GetIndex()])
	{
		return;
	}

	++BroadcastCount;
	Delegates.Broadcast(this, InFieldId);
}

void UFolioBindTest::Advance()
{
	// Set then report, which is what a view model's generated setters do.
	++Ticks;
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Ticks);

	const float NewHealth = 0.5f + 0.5f * FMath::Sin(Ticks * 0.05f);
	if (NewHealth != Health)
	{
		Health = NewHealth;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Health);
	}

	if (const bool bNewAlive = Health > 0.35f; bNewAlive != bAlive)
	{
		bAlive = bNewAlive;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::bAlive);

		Status = bAlive ? TEXT("alive") : TEXT("down");
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Status);
	}
}

#include "FolioObjectModel.h"

#include "FolioModule.h"

#include "INotifyFieldValueChanged.h"
#include "UObject/UnrealType.h"

#include <RmlUi/Core/Context.h>

namespace
{
	/** How a property is best represented in a data expression. */
	FFolioObjectModel::FField::EKind KindFor(const FProperty& Property)
	{
		using EKind = FFolioObjectModel::FField::EKind;

		if (Property.IsA<FBoolProperty>())
		{
			return EKind::Bool;
		}

		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			// An enum stored as a byte reads better as its name than as its ordinal.
			if (Numeric->IsEnum())
			{
				return EKind::String;
			}
			return Numeric->IsFloatingPoint() ? EKind::Float : EKind::Int;
		}

		// Everything else - FString, FName, FText, enums, structs, object pointers - goes through
		// ExportText, which every property type implements.
		return EKind::String;
	}

	bool IsBindable(const FProperty& Property)
	{
		// A container needs RmlUi's array or struct registration, which is per element type and
		// cannot be built from reflection alone. Reported once so the omission is visible.
		return !Property.IsA<FArrayProperty>() && !Property.IsA<FMapProperty>()
			&& !Property.IsA<FSetProperty>();
	}

	/** Whether a data expression can supply this parameter at all. */
	bool IsCallableParameter(const FProperty& Property)
	{
		// An out parameter has nowhere to go: RmlUi passes values by variant and reads nothing back.
		if (Property.HasAnyPropertyFlags(CPF_OutParm | CPF_ReferenceParm))
		{
			return false;
		}

		return Property.IsA<FBoolProperty>() || Property.IsA<FNumericProperty>()
			|| Property.IsA<FStrProperty>() || Property.IsA<FNameProperty>();
	}

	/** Writes one variant into one parameter slot of a call frame. */
	void WriteParameter(const FProperty& Property, void* Slot, const Rml::Variant& Value)
	{
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(&Property))
		{
			Bool->SetPropertyValue(Slot, Value.Get<bool>());
		}
		else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			if (Numeric->IsFloatingPoint())
			{
				Numeric->SetFloatingPointPropertyValue(Slot, Value.Get<float>());
			}
			else
			{
				Numeric->SetIntPropertyValue(Slot, static_cast<int64>(Value.Get<int>()));
			}
		}
		else if (Property.IsA<FStrProperty>())
		{
			*static_cast<FString*>(Slot) = Folio::ToFString(Value.Get<Rml::String>());
		}
		else if (Property.IsA<FNameProperty>())
		{
			*static_cast<FName*>(Slot) = FName(*Folio::ToFString(Value.Get<Rml::String>()));
		}
	}
}

FFolioObjectModel::~FFolioObjectModel()
{
	Unsubscribe();
}

bool FFolioObjectModel::Mount(Rml::Context* Context, const FString& InModelName, UObject* InSource,
	const bool bPollEveryFrame)
{
	if (!Context || !InSource || InModelName.IsEmpty())
	{
		return false;
	}

	Rml::DataModelConstructor Constructor = Context->CreateDataModel(Folio::ToRmlString(InModelName));
	if (!Constructor)
	{
		UE_LOG(LogFolio, Warning, TEXT("could not create data model '%s'"), *InModelName);
		return false;
	}

	Source = InSource;
	ModelName = InModelName;

	INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(InSource);
	int32 Skipped = 0;

	for (FProperty* Property = InSource->GetClass()->PropertyLink; Property;
		Property = Property->PropertyLinkNext)
	{
		if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_Edit))
		{
			// Internal state is not a binding surface. A view model marks what it publishes.
			continue;
		}

		if (!IsBindable(*Property))
		{
			++Skipped;
			continue;
		}

		TUniquePtr<FField> Field = MakeUnique<FField>();
		Field->Property = Property;
		Field->Name = Folio::ToRmlString(Property->GetName());
		Field->Kind = KindFor(*Property);
		Refresh(*Field);

		switch (Field->Kind)
		{
		case FField::EKind::Bool:   Constructor.Bind(Field->Name, &Field->bValue); break;
		case FField::EKind::Int:    Constructor.Bind(Field->Name, &Field->IntValue); break;
		case FField::EKind::Float:  Constructor.Bind(Field->Name, &Field->FloatValue); break;
		default:                    Constructor.Bind(Field->Name, &Field->StringValue); break;
		}

		Fields.Add(MoveTemp(Field));
	}

	// Commands, so markup can reach game code rather than only read from it. Bound before the handle
	// is taken, like everything else on a constructor.
	TArray<FString> Rejected;
	for (TFieldIterator<UFunction> It(InSource->GetClass()); It; ++It)
	{
		UFunction* Function = *It;
		if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
		{
			continue;
		}

		if (BindCommand(Constructor, Function))
		{
			continue;
		}

		// Only the source's own functions are worth reporting. Every base class contributes callable
		// functions nobody meant as UI commands, and naming those would bury the one that matters.
		if (Function->GetOwnerClass() == InSource->GetClass())
		{
			Rejected.Add(Function->GetName());
		}
	}

	Handle = Constructor.GetModelHandle();

	// Notification per field where the source supports it. A view model that reports its own changes
	// is the reason to use one, and it makes the per-frame sweep unnecessary.
	if (Notifier)
	{
		const UE::FieldNotification::IClassDescriptor& Descriptor = Notifier->GetFieldNotificationDescriptor();

		for (TUniquePtr<FField>& Field : Fields)
		{
			const UE::FieldNotification::FFieldId Id =
				Descriptor.GetField(InSource->GetClass(), Field->Property->GetFName());

			if (!Id.IsValid())
			{
				continue;
			}

			FField* Raw = Field.Get();
			Field->Notification = Notifier->AddFieldValueChangedDelegate(Id,
				INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda(
					[this, Raw](UObject*, UE::FieldNotification::FFieldId)
					{
						if (Refresh(*Raw) && Handle)
						{
							Handle.DirtyVariable(Raw->Name);
						}
					}));
		}
	}

	// Only where nothing reports changes. Polling a source that does would be pure waste.
	bPolling = bPollEveryFrame && Notifier == nullptr;

	UE_LOG(LogFolio, Log,
		TEXT("bound %d property(s) and %d command(s) of '%s' as data model '%s'%s%s"),
		Fields.Num(), Commands, *InSource->GetName(), *ModelName,
		Notifier ? TEXT(", updating by notification") : TEXT(""),
		bPolling ? TEXT(", polled every frame") : TEXT(""));

	if (Skipped > 0)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' has %d array, map or set property(s), which cannot be bound by reflection. "
				"Expose them as a bound array from game code instead."),
			*InSource->GetName(), Skipped);
	}

	if (Rejected.Num() > 0)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' cannot expose %s to markup. A command needs no return value and only bool, "
				"number, string or name parameters, passed by value."),
			*InSource->GetName(), *FString::Join(Rejected, TEXT(", ")));
	}

	return true;
}

bool FFolioObjectModel::BindCommand(Rml::DataModelConstructor& Constructor, UFunction* Function)
{
	if (Function->GetReturnProperty() != nullptr)
	{
		// A data expression discards whatever a callback returns, so a function with a result is
		// almost certainly a query someone meant to bind as a value.
		return false;
	}

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (!IsCallableParameter(**It))
		{
			return false;
		}
	}

	// Weak, because the model outlives nothing but the function belongs to a class that a live coding
	// pass can replace underneath it.
	const TWeakObjectPtr<UFunction> Weak(Function);

	if (!Constructor.BindEventCallback(Folio::ToRmlString(Function->GetName()),
		[this, Weak](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& Arguments)
		{
			if (UFunction* Target = Weak.Get())
			{
				Invoke(Target, Arguments);
			}
		}))
	{
		return false;
	}

	++Commands;
	return true;
}

void FFolioObjectModel::Invoke(UFunction* Function, const Rml::VariantList& Arguments) const
{
	UObject* Object = Source.Get();
	if (!Object)
	{
		return;
	}

	// ProcessEvent wants one contiguous frame holding every parameter at its reflected offset. The
	// frame has to be constructed and destroyed by the properties themselves, not memset: an FString
	// parameter left as zeroed memory is a string that frees a pointer it never owned.
	void* Frame = FMemory_Alloca_Aligned(FMath::Max<int32>(Function->ParmsSize, 1), Function->MinAlignment);
	FMemory::Memzero(Frame, Function->ParmsSize);

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Frame);
	}

	int32 Index = 0;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
	{
		// Anything the expression did not supply keeps the value the property initialised to, so
		// data-event-click="Confirm" works on a function that happens to take an optional argument.
		if (Index < static_cast<int32>(Arguments.size()))
		{
			WriteParameter(**It, It->ContainerPtrToValuePtr<void>(Frame), Arguments[Index]);
		}
	}

	Object->ProcessEvent(Function, Frame);

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Frame);
	}
}

void FFolioObjectModel::Tick()
{
	if (!bPolling || !Handle)
	{
		return;
	}

	if (!Source.IsValid())
	{
		// The source was collected. Stop rather than read a stale object every frame.
		bPolling = false;
		return;
	}

	for (TUniquePtr<FField>& Field : Fields)
	{
		if (Refresh(*Field))
		{
			Handle.DirtyVariable(Field->Name);
		}
	}
}

bool FFolioObjectModel::Refresh(FField& Field)
{
	UObject* Object = Source.Get();
	if (!Object || !Field.Property)
	{
		return false;
	}

	switch (Field.Kind)
	{
	case FField::EKind::Bool:
	{
		const FBoolProperty* Bool = CastFieldChecked<FBoolProperty>(Field.Property);
		const bool bNew = Bool->GetPropertyValue_InContainer(Object);
		if (bNew == Field.bValue) { return false; }
		Field.bValue = bNew;
		return true;
	}

	case FField::EKind::Int:
	{
		const FNumericProperty* Numeric = CastFieldChecked<FNumericProperty>(Field.Property);
		const int New = static_cast<int>(
			Numeric->GetSignedIntPropertyValue(Field.Property->ContainerPtrToValuePtr<void>(Object)));
		if (New == Field.IntValue) { return false; }
		Field.IntValue = New;
		return true;
	}

	case FField::EKind::Float:
	{
		const FNumericProperty* Numeric = CastFieldChecked<FNumericProperty>(Field.Property);
		const float New = static_cast<float>(
			Numeric->GetFloatingPointPropertyValue(Field.Property->ContainerPtrToValuePtr<void>(Object)));
		if (New == Field.FloatValue) { return false; }
		Field.FloatValue = New;
		return true;
	}

	default:
	{
		FString Text;
		Field.Property->ExportTextItem_InContainer(Text, Object, nullptr, Object, PPF_None);

		const Rml::String New = Folio::ToRmlString(Text);
		if (New == Field.StringValue) { return false; }
		Field.StringValue = New;
		return true;
	}
	}
}

void FFolioObjectModel::Unsubscribe()
{
	if (INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(Source.Get()))
	{
		const UE::FieldNotification::IClassDescriptor& Descriptor = Notifier->GetFieldNotificationDescriptor();

		for (TUniquePtr<FField>& Field : Fields)
		{
			if (!Field->Notification.IsValid())
			{
				continue;
			}

			const UE::FieldNotification::FFieldId Id =
				Descriptor.GetField(Source->GetClass(), Field->Property->GetFName());

			if (Id.IsValid())
			{
				Notifier->RemoveFieldValueChangedDelegate(Id, Field->Notification);
			}
		}
	}

	Fields.Empty();
	Handle = Rml::DataModelHandle();
	Source.Reset();
}

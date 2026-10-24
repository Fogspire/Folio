#include "FolioObjectModel.h"

#include "FolioModule.h"

#include "INotifyFieldValueChanged.h"
#include "UObject/UnrealType.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
	using FValue = FFolioObjectModel::FValue;

	bool IsContainer(const FProperty& Property)
	{
		return Property.IsA<FArrayProperty>() || Property.IsA<FMapProperty>()
			|| Property.IsA<FSetProperty>();
	}

	/** How a property is best represented in a data expression. */
	FValue::EKind KindFor(const FProperty& Property)
	{
		using EKind = FValue::EKind;

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

		// Everything else - FString, FName, FText, enums, structs, object pointers - reads as a
		// string. The three text types are read directly and everything else through ExportText,
		// which every property type implements.
		return EKind::String;
	}

	bool IsBindable(const FProperty& Property)
	{
		// A map or set has no ordering a document could iterate, and RmlUi's data model has no shape
		// for either. An array does, and is bound as one.
		return !Property.IsA<FMapProperty>() && !Property.IsA<FSetProperty>();
	}

	/** Reads one value out of the object graph and into its mirror. True when it moved. */
	bool ReadInto(FValue& Out, const FProperty& Property, const void* ValuePtr, UObject* Parent)
	{
		switch (Out.Kind)
		{
		case FValue::EKind::Bool:
		{
			const bool bNew = CastFieldChecked<const FBoolProperty>(&Property)->GetPropertyValue(ValuePtr);
			if (bNew == Out.bValue) { return false; }
			Out.bValue = bNew;
			return true;
		}

		case FValue::EKind::Int:
		{
			const int New = static_cast<int>(
				CastFieldChecked<const FNumericProperty>(&Property)->GetSignedIntPropertyValue(ValuePtr));
			if (New == Out.IntValue) { return false; }
			Out.IntValue = New;
			return true;
		}

		case FValue::EKind::Float:
		{
			const float New = static_cast<float>(
				CastFieldChecked<const FNumericProperty>(&Property)->GetFloatingPointPropertyValue(ValuePtr));
			if (New == Out.FloatValue) { return false; }
			Out.FloatValue = New;
			return true;
		}

		default:
		{
			FString Text;

			/*
			 * The text types are read rather than exported. ExportTextItem writes the *source form*
			 * of a property, which for an FText carrying a namespace and key is the whole
			 * NSLOCTEXT("ns", "key", "source") expression - so every properly localised string
			 * bound into a document rendered as its own declaration. A text built at runtime has
			 * no key and exports as its contents, which is why this survived: the bug only appears
			 * once a string is localised.
			 *
			 * FString and FName are here for the same reason in miniature: ExportText quotes and
			 * escapes anything with a delimiter in it, so a name with a space in it arrived quoted.
			 */
			if (const FTextProperty* AsText = CastField<FTextProperty>(&Property))
			{
				Text = AsText->GetPropertyValue(ValuePtr).ToString();
			}
			else if (const FStrProperty* AsString = CastField<FStrProperty>(&Property))
			{
				Text = AsString->GetPropertyValue(ValuePtr);
			}
			else if (const FNameProperty* AsName = CastField<FNameProperty>(&Property))
			{
				Text = AsName->GetPropertyValue(ValuePtr).ToString();
			}
			else
			{
				Property.ExportTextItem_Direct(Text, ValuePtr, nullptr, Parent, PPF_None);
			}

			const Rml::String New = Folio::ToRmlString(Text);
			if (New == Out.StringValue) { return false; }
			Out.StringValue = New;
			return true;
		}
		}
	}

	/** Puts a mirror back to nothing, for a row whose object went away. */
	bool ResetValue(FValue& Out)
	{
		const FValue Empty{ Out.Kind };
		if (Out.bValue == Empty.bValue && Out.IntValue == Empty.IntValue
			&& Out.FloatValue == Empty.FloatValue && Out.StringValue == Empty.StringValue)
		{
			return false;
		}

		Out = Empty;
		return true;
	}

	/** The properties of a struct or class a document is allowed to read. */
	void CollectMembers(const UStruct& Owner, TArray<FProperty*>& Out)
	{
		for (FProperty* Property = Owner.PropertyLink; Property; Property = Property->PropertyLinkNext)
		{
			if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_Edit) || IsContainer(*Property))
			{
				continue;
			}

			Out.Add(Property);
		}
	}

	/** Deep enough for a prompt's glyph inside a row, shallow enough that a struct graph cannot run away. */
	constexpr int32 MaxLeafDepth = 4;

	/** A mirror for one member, with mirrors for its own members when it is a struct. */
	TUniquePtr<FFolioObjectModel::FLeaf> MakeLeaf(FProperty* Property, const int32 Depth)
	{
		TUniquePtr<FFolioObjectModel::FLeaf> Leaf = MakeUnique<FFolioObjectModel::FLeaf>();
		Leaf->Property = Property;
		Leaf->Name = Folio::ToRmlString(Property->GetName());
		Leaf->Value.Kind = KindFor(*Property);

		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		if (Struct && Depth < MaxLeafDepth)
		{
			TArray<FProperty*> Members;
			CollectMembers(*Struct->Struct, Members);
			for (FProperty* Member : Members)
			{
				Leaf->Members.Add(MakeLeaf(Member, Depth + 1));
			}
		}

		return Leaf;
	}

	/** Reads a member, and its members, out of the element or struct holding it. True when anything moved. */
	bool ReadLeaf(FFolioObjectModel::FLeaf& Leaf, const void* Container, UObject* Parent)
	{
		const void* ValuePtr = Leaf.Property->ContainerPtrToValuePtr<void>(Container);
		bool bMoved = ReadInto(Leaf.Value, *Leaf.Property, ValuePtr, Parent);
		for (TUniquePtr<FFolioObjectModel::FLeaf>& Member : Leaf.Members)
		{
			bMoved |= ReadLeaf(*Member, ValuePtr, Parent);
		}
		return bMoved;
	}

	bool ResetLeaf(FFolioObjectModel::FLeaf& Leaf)
	{
		bool bMoved = ResetValue(Leaf.Value);
		for (TUniquePtr<FFolioObjectModel::FLeaf>& Member : Leaf.Members)
		{
			bMoved |= ResetLeaf(*Member);
		}
		return bMoved;
	}

	/** Whether a data expression can supply this parameter at all. */
	bool IsCallableParameter(const FProperty& Property)
	{
		// An out parameter has nowhere to go: RmlUi passes values by variant and reads nothing back.
		//
		// A *const* reference is an input, not an output, and `const FString&` is how anyone would
		// write a UFUNCTION that takes a string. Rejecting it silently dropped the whole command, so
		// the only symptom was a button that did nothing.
		if (Property.HasAnyPropertyFlags(CPF_OutParm))
		{
			return false;
		}

		if (Property.HasAnyPropertyFlags(CPF_ReferenceParm) && !Property.HasAnyPropertyFlags(CPF_ConstParm))
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

/**
 * How RmlUi reads and writes everything this model binds.
 *
 * Five definitions, held together because the array ones hand out the others. A definition carries no
 * state of its own: RmlUi pairs it with a pointer, and which mirror that is decides what gets read.
 */
struct FFolioObjectModel::FDefinitions
{
	/**
	 * A bound property.
	 *
	 * Bound by address of the FField rather than of the mirrored value, which is what makes a write
	 * reach the UObject: given only a `float*` RmlUi would set the mirror, the next refresh would
	 * overwrite it from the property, and a slider would spring back one frame after being dragged.
	 */
	class FFieldDefinition final : public Rml::VariableDefinition
	{
	public:
		FFieldDefinition() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* Ptr, Rml::Variant& Variant) override
		{
			return Read(static_cast<FField*>(Ptr)->Value, Variant);
		}

		bool Set(void* Ptr, const Rml::Variant& Variant) override
		{
			FField& Field = *static_cast<FField*>(Ptr);
			return Field.Owner != nullptr && Field.Owner->Write(Field, Variant);
		}

		static bool Read(const FValue& Value, Rml::Variant& Variant)
		{
			switch (Value.Kind)
			{
			case FValue::EKind::Bool:  Variant = Value.bValue; break;
			case FValue::EKind::Int:   Variant = Value.IntValue; break;
			case FValue::EKind::Float: Variant = Value.FloatValue; break;
			default:                   Variant = Value.StringValue; break;
			}
			return true;
		}
	};

	/** A mirror inside an array. Read only: an element is a copy, so a write would go nowhere. */
	class FValueDefinition final : public Rml::VariableDefinition
	{
	public:
		FValueDefinition() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* Ptr, Rml::Variant& Variant) override
		{
			return FFieldDefinition::Read(*static_cast<FValue*>(Ptr), Variant);
		}
	};

	/**
	 * A row that is no longer there.
	 *
	 * An array shrinking is ordinary: a kill feed drops its oldest line every few seconds. RmlUi's
	 * data-for still holds bindings to the rows it had when the frame started, and it reads them
	 * before it rebuilds, so the frame an array shrinks is a frame of reads past its end. Answering
	 * those with an invalid variable is what makes RmlUi log every one of them as an error.
	 *
	 * This answers anything asked of it, with nothing. The view rebuilds a moment later and the
	 * reads land on real rows again.
	 */
	class FVoidDefinition final : public Rml::VariableDefinition
	{
	public:
		FVoidDefinition() : VariableDefinition(Rml::DataVariableType::Struct) {}

		bool Get(void*, Rml::Variant& Variant) override
		{
			Variant = Rml::String();
			return true;
		}

		Rml::DataVariable Child(void* Ptr, const Rml::DataAddressEntry&) override
		{
			return Rml::DataVariable(this, Ptr);
		}
	};

	/** One element of an array of structs or objects, addressed by member name. */
	class FRowDefinition final : public Rml::VariableDefinition
	{
	public:
		explicit FRowDefinition(FDefinitions& InOwner)
			: VariableDefinition(Rml::DataVariableType::Struct), Owner(InOwner) {}

		Rml::DataVariable Child(void* Ptr, const Rml::DataAddressEntry& Address) override
		{
			return Owner.FindMember(static_cast<FRow*>(Ptr)->Members, Address);
		}

	private:
		FDefinitions& Owner;
	};

	/** A struct member inside a row: its exported text as a value, and its own members by name. */
	class FNestedDefinition final : public Rml::VariableDefinition
	{
	public:
		explicit FNestedDefinition(FDefinitions& InOwner)
			: VariableDefinition(Rml::DataVariableType::Struct), Owner(InOwner) {}

		bool Get(void* Ptr, Rml::Variant& Variant) override
		{
			return FFieldDefinition::Read(static_cast<FLeaf*>(Ptr)->Value, Variant);
		}

		Rml::DataVariable Child(void* Ptr, const Rml::DataAddressEntry& Address) override
		{
			return Owner.FindMember(static_cast<FLeaf*>(Ptr)->Members, Address);
		}

	private:
		FDefinitions& Owner;
	};

	Rml::DataVariable FindMember(TArray<TUniquePtr<FLeaf>>& Members, const Rml::DataAddressEntry& Address)
	{
		for (TUniquePtr<FLeaf>& Leaf : Members)
		{
			// A property's name is an FName, which outside the editor keeps whichever spelling was created first:
			// `Position` comes back as `position` when something interned that earlier.
			if (Rml::StringUtilities::StringCompareCaseInsensitive(Leaf->Name, Address.name))
			{
				return Leaf->Members.IsEmpty()
					? Rml::DataVariable(&Mirror, &Leaf->Value)
					: Rml::DataVariable(&Nested, Leaf.Get());
			}
		}

		return Rml::DataVariable();
	}

	/**
	 * How many elements an array has, as a scalar.
	 *
	 * RmlUi calls Size() from data-for and nowhere else, so nothing in a data expression can reach a
	 * length without this. Matches what the JSON mock models expose, so a document counts the same in
	 * the preview tool and in the engine.
	 */
	class FCountDefinition final : public Rml::VariableDefinition
	{
	public:
		FCountDefinition() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* Ptr, Rml::Variant& Variant) override
		{
			Variant = static_cast<FArrayField*>(Ptr)->Rows.Num();
			return true;
		}
	};

	class FArrayDefinition final : public Rml::VariableDefinition
	{
	public:
		explicit FArrayDefinition(FDefinitions& InOwner)
			: VariableDefinition(Rml::DataVariableType::Array), Owner(InOwner) {}

		int Size(void* Ptr) override
		{
			return static_cast<FArrayField*>(Ptr)->Rows.Num();
		}

		Rml::DataVariable Child(void* Ptr, const Rml::DataAddressEntry& Address) override
		{
			FArrayField& Field = *static_cast<FArrayField*>(Ptr);

			if (Address.index < 0)
			{
				// An array has no named members of its own, so this shadows nothing.
				return Address.name == "size" ? Rml::DataVariable(&Owner.Count, Ptr) : Rml::DataVariable();
			}

			if (!Field.Rows.IsValidIndex(Address.index))
			{
				return Rml::DataVariable(&Owner.Gone, Ptr);
			}

			FRow& Row = *Field.Rows[Address.index];
			return Row.Scalar
				? Rml::DataVariable(&Owner.Mirror, &Row.Scalar->Value)
				: Rml::DataVariable(&Owner.Element, &Row);
		}

	private:
		FDefinitions& Owner;
	};

	FFieldDefinition Bound;
	FValueDefinition Mirror;
	FCountDefinition Count;
	FRowDefinition Element{ *this };
	FNestedDefinition Nested{ *this };
	FArrayDefinition List{ *this };
	FVoidDefinition Gone;
};

FFolioObjectModel::FFolioObjectModel()
	: Definitions(MakeUnique<FDefinitions>())
{
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

#if WITH_EDITOR
	FFolioModule::Get().RecordViewModel(InModelName, InSource->GetClass());
#endif

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

		if (FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			if (!BindArray(Constructor, *Array))
			{
				++Skipped;
			}
			continue;
		}

		TUniquePtr<FField> Field = MakeUnique<FField>();
		Field->Property = Property;
		Field->Name = Folio::ToRmlString(Property->GetName());
		Field->Value.Kind = KindFor(*Property);
		Field->Owner = this;

		// BlueprintReadOnly and EditConst are the author saying this is published for display. A
		// markup write to one is refused rather than half-applied.
		Field->bWritable = !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst);

		Refresh(*Field);

		// Through the field rather than the mirrored value, so a write can reach the property. See
		// FFieldDefinition.
		Constructor.BindCustomDataVariable(Field->Name,
			Rml::DataVariable(&Definitions->Bound, Field.Get()));

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

			Field->FieldId = Id;

			FField* Raw = Field.Get();
			Field->Notification = Notifier->AddFieldValueChangedDelegate(Id,
				INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda(
					[this, Raw](UObject*, UE::FieldNotification::FFieldId)
					{
						if (Refresh(*Raw) && Handle)
						{
							Handle.DirtyVariable(Raw->Name);

							// Recorded rather than acted on: this fires whenever the source chooses,
							// and the canvas asks once a frame.
							bNotified = true;
						}
					}));
		}

		for (TUniquePtr<FArrayField>& Array : ArrayFields)
		{
			const UE::FieldNotification::FFieldId Id =
				Descriptor.GetField(InSource->GetClass(), Array->Property->GetFName());

			if (!Id.IsValid())
			{
				continue;
			}

			Array->FieldId = Id;

			FArrayField* Raw = Array.Get();
			Array->Notification = Notifier->AddFieldValueChangedDelegate(Id,
				INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda(
					[this, Raw](UObject*, UE::FieldNotification::FFieldId)
					{
						if (Refresh(*Raw) && Handle)
						{
							Handle.DirtyVariable(Raw->Name);
							bNotified = true;
						}
					}));
		}
	}

	// Only where nothing reports changes. Polling a source that does would be pure waste.
	bPolling = bPollEveryFrame && Notifier == nullptr;

	UE_LOG(LogFolio, Log,
		TEXT("bound %d property(s), %d array(s) and %d command(s) of '%s' as data model '%s'%s%s"),
		Fields.Num(), ArrayFields.Num(), Commands, *InSource->GetName(), *ModelName,
		Notifier ? TEXT(", updating by notification") : TEXT(""),
		bPolling ? TEXT(", polled every frame") : TEXT(""));

	if (Skipped > 0)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' has %d property(s) that cannot be bound by reflection. Maps and sets have no "
				"shape in a data model, and an array of arrays has no element to bind."),
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

bool FFolioObjectModel::BindArray(Rml::DataModelConstructor& Constructor, FArrayProperty& Property)
{
	FProperty* Inner = Property.Inner;
	if (!Inner || IsContainer(*Inner))
	{
		return false;
	}

	TUniquePtr<FArrayField> Field = MakeUnique<FArrayField>();
	Field->Property = &Property;
	Field->Name = Folio::ToRmlString(Property.GetName());

	if (const FStructProperty* Struct = CastField<FStructProperty>(Inner))
	{
		Field->Element = FArrayField::EElement::Struct;
		CollectMembers(*Struct->Struct, Field->MemberProperties);
	}
	else if (const FObjectProperty* Object = CastField<FObjectProperty>(Inner))
	{
		// The declared class, not each element's own: a member list resolved per element would change
		// shape as the array is refilled, and RmlUi resolves a member name against one shape.
		Field->Element = FArrayField::EElement::Object;
		CollectMembers(*Object->PropertyClass, Field->MemberProperties);
	}
	else
	{
		Field->Element = FArrayField::EElement::Scalar;
		Field->ScalarKind = KindFor(*Inner);
	}

	if (Field->Element != FArrayField::EElement::Scalar && Field->MemberProperties.IsEmpty())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' has no property a document can read, so '%s' binds as an array of nothing."),
			*Inner->GetCPPType(), *Property.GetName());
	}

	Refresh(*Field);

	Constructor.BindCustomDataVariable(Field->Name,
		Rml::DataVariable(&Definitions->List, Field.Get()));

	ArrayFields.Add(MoveTemp(Field));
	return true;
}

TUniquePtr<FFolioObjectModel::FRow> FFolioObjectModel::MakeRow(const FArrayField& Field) const
{
	TUniquePtr<FRow> Row = MakeUnique<FRow>();

	if (Field.Element == FArrayField::EElement::Scalar)
	{
		Row->Scalar = MakeUnique<FLeaf>();
		Row->Scalar->Property = Field.Property->Inner;
		Row->Scalar->Value.Kind = Field.ScalarKind;
		return Row;
	}

	Row->Members.Reserve(Field.MemberProperties.Num());
	for (FProperty* Member : Field.MemberProperties)
	{
		Row->Members.Add(MakeLeaf(Member, 1));
	}

	return Row;
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

bool FFolioObjectModel::Tick()
{
	// A notification arrived since the last tick, which already dirtied its variable. Reported here
	// because the canvas asks once a frame rather than being called back.
	const bool bWasNotified = bNotified;
	bNotified = false;

	if (!bPolling || !Handle)
	{
		return bWasNotified;
	}

	if (!Source.IsValid())
	{
		// The source was collected. Stop rather than read a stale object every frame.
		bPolling = false;
		return bWasNotified;
	}

	bool bMoved = false;
	for (TUniquePtr<FField>& Field : Fields)
	{
		if (Refresh(*Field))
		{
			Handle.DirtyVariable(Field->Name);
			bMoved = true;
		}
	}

	for (TUniquePtr<FArrayField>& Field : ArrayFields)
	{
		if (Refresh(*Field))
		{
			Handle.DirtyVariable(Field->Name);
			bMoved = true;
		}
	}

	return bMoved || bWasNotified;
}

bool FFolioObjectModel::Refresh(FField& Field)
{
	UObject* Object = Source.Get();
	if (!Object || !Field.Property)
	{
		return false;
	}

	return ReadInto(Field.Value, *Field.Property,
		Field.Property->ContainerPtrToValuePtr<void>(Object), Object);
}

bool FFolioObjectModel::Refresh(FArrayField& Field)
{
	UObject* Object = Source.Get();
	if (!Object || !Field.Property)
	{
		return false;
	}

	FScriptArrayHelper Helper(Field.Property, Field.Property->ContainerPtrToValuePtr<void>(Object));
	const int32 Num = Helper.Num();

	bool bMoved = false;

	while (Field.Rows.Num() > Num)
	{
		Field.Rows.Pop();
		bMoved = true;
	}

	while (Field.Rows.Num() < Num)
	{
		Field.Rows.Add(MakeRow(Field));
		bMoved = true;
	}

	for (int32 Index = 0; Index < Num; ++Index)
	{
		FRow& Row = *Field.Rows[Index];
		const void* Element = Helper.GetRawPtr(Index);

		if (Row.Scalar)
		{
			bMoved |= ReadInto(Row.Scalar->Value, *Row.Scalar->Property, Element, Object);
			continue;
		}

		if (Field.Element == FArrayField::EElement::Object)
		{
			Element = CastFieldChecked<FObjectProperty>(Field.Property->Inner)
				->GetObjectPropertyValue(Element);
		}

		for (TUniquePtr<FLeaf>& Leaf : Row.Members)
		{
			// A null element still occupies a row, so it blanks rather than reading through nothing.
			bMoved |= Element ? ReadLeaf(*Leaf, Element, Object) : ResetLeaf(*Leaf);
		}
	}

	return bMoved;
}

bool FFolioObjectModel::Write(FField& Field, const Rml::Variant& Value)
{
	UObject* Object = Source.Get();
	if (!Object || !Field.Property)
	{
		return false;
	}

	if (!Field.bWritable)
	{
		// Once per field. A slider bound to a read-only value simply will not move, and without this
		// the reason is invisible.
		if (!Field.bWarnedReadOnly)
		{
			Field.bWarnedReadOnly = true;
			UE_LOG(LogFolio, Warning,
				TEXT("'%s.%s' is read-only, so markup cannot write it. Drop BlueprintReadOnly or "
					"EditConst for a value a control is meant to change."),
				*Object->GetName(), *Field.Property->GetName());
		}
		return false;
	}

	void* Slot = Field.Property->ContainerPtrToValuePtr<void>(Object);

	switch (Field.Value.Kind)
	{
	case FValue::EKind::Bool:
		CastFieldChecked<FBoolProperty>(Field.Property)->SetPropertyValue(Slot, Value.Get<bool>());
		break;

	case FValue::EKind::Int:
		CastFieldChecked<FNumericProperty>(Field.Property)
			->SetIntPropertyValue(Slot, static_cast<int64>(Value.Get<int>()));
		break;

	case FValue::EKind::Float:
		CastFieldChecked<FNumericProperty>(Field.Property)
			->SetFloatingPointPropertyValue(Slot, Value.Get<float>());
		break;

	default:
	{
		// The inverse of the ExportTextItem the read path uses, so anything that can be read as a
		// string can be written back the same way: an FName, an FText, an enum by name, a struct.
		const FString Text = Folio::ToFString(Value.Get<Rml::String>());
		if (Field.Property->ImportText_InContainer(*Text, Object, Object, PPF_None) == nullptr)
		{
			UE_LOG(LogFolio, Warning, TEXT("could not write '%s' to %s.%s"),
				*Text, *Object->GetName(), *Field.Property->GetName());
			return false;
		}
		break;
	}
	}

	// Read back rather than trusting the value written: a property may clamp, quantise or round, and
	// the document has to show what the object actually holds.
	Refresh(Field);

	// Announced, so anything else bound to the same field updates. A settings screen showing the same
	// value twice - a slider and a number beside it - is the ordinary case, and without this only the
	// control that was touched moves.
	if (Field.FieldId.IsValid())
	{
		if (INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(Object))
		{
			Notifier->BroadcastFieldValueChanged(Field.FieldId);
		}
	}

	return true;
}

void FFolioObjectModel::Unsubscribe()
{
	if (INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(Source.Get()))
	{
		const UE::FieldNotification::IClassDescriptor& Descriptor = Notifier->GetFieldNotificationDescriptor();

		for (TUniquePtr<FField>& Field : Fields)
		{
			if (Field->Notification.IsValid() && Field->FieldId.IsValid())
			{
				Notifier->RemoveFieldValueChangedDelegate(Field->FieldId, Field->Notification);
			}
		}

		for (TUniquePtr<FArrayField>& Field : ArrayFields)
		{
			if (Field->Notification.IsValid() && Field->FieldId.IsValid())
			{
				Notifier->RemoveFieldValueChangedDelegate(Field->FieldId, Field->Notification);
			}
		}
	}

	Fields.Empty();
	ArrayFields.Empty();
	Handle = Rml::DataModelHandle();
	Source.Reset();
}

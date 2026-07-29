#include "FolioDataModel.h"

#include "FolioModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataVariable.h>

/**
 * One definition per JSON kind, shared by every node of that kind. The node pointer carries all
 * the state, so children handed back from Child need no registration of their own.
 */
class FFolioDataModel::FDefinitions
{
public:
	Rml::DataVariable For(FJsonValue* Node)
	{
		if (!Node)
		{
			return Rml::DataVariable();
		}

		if (Node->Type == EJson::Object)
		{
			return Rml::DataVariable(&Structure, Node);
		}

		if (Node->Type == EJson::Array)
		{
			return Rml::DataVariable(&Array, Node);
		}

		return Rml::DataVariable(&Scalar, Node);
	}

	class FScalar final : public Rml::VariableDefinition
	{
	public:
		FScalar() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* Ptr, Rml::Variant& Variant) override
		{
			const FJsonValue& Node = *static_cast<FJsonValue*>(Ptr);
			switch (Node.Type)
			{
			case EJson::String:
				Variant = Folio::ToRmlString(Node.AsString());
				break;

			case EJson::Boolean:
				Variant = Node.AsBool();
				break;

			case EJson::Number:
			{
				// Whole numbers bind as integers so a count reads "3" rather than "3.000000".
				const double Value = Node.AsNumber();
				if (FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value)) && FMath::Abs(Value) < double(MAX_int32))
				{
					Variant = int(FMath::RoundToDouble(Value));
				}
				else
				{
					Variant = float(Value);
				}
				break;
			}

			default:
				Variant = Rml::String();
				break;
			}

			return true;
		}

		bool Set(void*, const Rml::Variant&) override
		{
			// Mock data is read-only: nothing in the editor owns it to write back to.
			return false;
		}
	};

	/**
	 * How many entries an array has, as a scalar.
	 *
	 * RmlUi calls VariableDefinition::Size() from data-for and nowhere else, so nothing in a data
	 * expression can reach an array's length: `{{ Items.size }}` and `{{ Items | size }}` both
	 * fail, and a transform function cannot help because transforms take Variants and an array
	 * cannot produce one. Every screen that wants "3 items" or an empty-list message needs it.
	 *
	 * Must stay in step with the preview tool's copy, or a document counts differently in the
	 * editor than in the tool it was authored in.
	 */
	class FCount final : public Rml::VariableDefinition
	{
	public:
		FCount() : VariableDefinition(Rml::DataVariableType::Scalar) {}

		bool Get(void* Ptr, Rml::Variant& Variant) override
		{
			Variant = static_cast<FJsonValue*>(Ptr)->AsArray().Num();
			return true;
		}
	};

	class FArray final : public Rml::VariableDefinition
	{
	public:
		explicit FArray(FDefinitions& InOwner) : VariableDefinition(Rml::DataVariableType::Array), Owner(InOwner) {}

		int Size(void* Ptr) override
		{
			return static_cast<FJsonValue*>(Ptr)->AsArray().Num();
		}

		Rml::DataVariable Child(void* Ptr, const Rml::DataAddressEntry& Address) override
		{
			// An array has no named members of its own, so this shadows nothing.
			if (Address.index < 0 && Address.name == "size")
			{
				return Rml::DataVariable(&Owner.Count, Ptr);
			}

			const TArray<TSharedPtr<FJsonValue>>& Values = static_cast<FJsonValue*>(Ptr)->AsArray();
			if (!Values.IsValidIndex(Address.index))
			{
				return Rml::DataVariable();
			}

			return Owner.For(Values[Address.index].Get());
		}

	private:
		FDefinitions& Owner;
	};

	class FStruct final : public Rml::VariableDefinition
	{
	public:
		explicit FStruct(FDefinitions& InOwner) : VariableDefinition(Rml::DataVariableType::Struct), Owner(InOwner) {}

		Rml::DataVariable Child(void* Ptr, const Rml::DataAddressEntry& Address) override
		{
			const TSharedPtr<FJsonObject>& Object = static_cast<FJsonValue*>(Ptr)->AsObject();
			if (!Object.IsValid())
			{
				return Rml::DataVariable();
			}

			const TSharedPtr<FJsonValue> Found = Object->TryGetField(Folio::ToFString(Address.name));
			return Found.IsValid() ? Owner.For(Found.Get()) : Rml::DataVariable();
		}

	private:
		FDefinitions& Owner;
	};

	FScalar Scalar;
	FCount Count;
	FArray Array {*this};
	FStruct Structure {*this};
};

FFolioDataModel::FFolioDataModel()
	: Definitions(MakeUnique<FDefinitions>())
{
}

FFolioDataModel::~FFolioDataModel() = default;

FString FFolioDataModel::DiscoverModelName(const FString& DocumentText)
{
	const FString Marker = TEXT("data-model");

	int32 At = DocumentText.Find(Marker, ESearchCase::IgnoreCase);
	if (At == INDEX_NONE)
	{
		return FString();
	}

	At = DocumentText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
	if (At == INDEX_NONE)
	{
		return FString();
	}

	const int32 End = DocumentText.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1);
	return End == INDEX_NONE ? FString() : DocumentText.Mid(At + 1, End - At - 1);
}

FString FFolioDataModel::FindMockFile(const FString& ContentRoot, const FString& ModelName)
{
	const FString Candidate = ContentRoot / TEXT("ui") / TEXT("mock") / (ModelName + TEXT(".json"));
	return FPaths::FileExists(Candidate) ? Candidate : FString();
}

bool FFolioDataModel::Mount(Rml::Context* Context, const FString& ModelName, const FString& JsonPath)
{
	if (!Context || ModelName.IsEmpty())
	{
		return false;
	}

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath))
	{
		UE_LOG(LogFolio, Warning, TEXT("could not read '%s'"), *JsonPath);
		return false;
	}

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		UE_LOG(LogFolio, Warning, TEXT("could not parse '%s'"), *JsonPath);
		return false;
	}

	// A file may hold the model directly, or several models keyed by name.
	if (const TSharedPtr<FJsonObject>* Nested = nullptr;
		Parsed->TryGetObjectField(ModelName, Nested) && Nested && Nested->IsValid())
	{
		Parsed = *Nested;
	}

	Rml::DataModelConstructor Constructor = Context->CreateDataModel(Folio::ToRmlString(ModelName));
	if (!Constructor)
	{
		UE_LOG(LogFolio, Warning, TEXT("could not create data model '%s'"), *ModelName);
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Parsed->Values)
	{
		if (!Entry.Value.IsValid())
		{
			continue;
		}

		// Held before binding: RmlUi keeps the address, so the value has to outlive this loop.
		Slots.Add(Entry.Key, Entry.Value);
		Constructor.BindCustomDataVariable(Folio::ToRmlString(Entry.Key), Definitions->For(Entry.Value.Get()));
	}

	Handle = Constructor.GetModelHandle();
	bMounted = true;

	UE_LOG(LogFolio, Log, TEXT("mounted data model '%s' from '%s'"), *ModelName, *JsonPath);
	return true;
}

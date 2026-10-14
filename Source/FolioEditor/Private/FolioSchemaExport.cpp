#include "FolioSchemaExport.h"

#include "FolioDocumentPath.h"
#include "FolioModule.h"

#include "Dom/JsonObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Logging/MessageLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "Folio"

namespace
{
	/** The enum behind a property, whether it is declared as an enum class or as a byte. */
	const UEnum* EnumOf(const FProperty& Property)
	{
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
		{
			return Enum->GetEnum();
		}
		if (const FByteProperty* Byte = CastField<FByteProperty>(&Property))
		{
			return Byte->Enum;
		}
		return nullptr;
	}

	/** A schema's name for a scalar property's type, or empty for one that is not a scalar. */
	FString ScalarType(const FProperty& Property)
	{
		if (Property.IsA<FBoolProperty>())
		{
			return TEXT("bool");
		}
		if (EnumOf(Property))
		{
			return TEXT("enum");
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			return Numeric->IsFloatingPoint() ? TEXT("float") : TEXT("int");
		}
		if (Property.IsA<FTextProperty>())
		{
			return TEXT("text");
		}
		if (Property.IsA<FNameProperty>())
		{
			return TEXT("name");
		}
		if (Property.IsA<FStrProperty>())
		{
			return TEXT("string");
		}
		return FString();
	}

	/** Describes one scalar property. Anything Folio would bind as its exported text is a string. */
	TSharedRef<FJsonObject> DescribeScalar(const FProperty& Property)
	{
		TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();

		const FString Type = ScalarType(Property);
		Field->SetStringField(TEXT("type"), Type.IsEmpty() ? TEXT("string") : Type);

		if (const UEnum* Enum = EnumOf(Property))
		{
			Field->SetStringField(TEXT("enum"), Enum->CppType.IsEmpty() ? Enum->GetName() : Enum->CppType);
		}

		const FString Tooltip = Property.GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty())
		{
			Field->SetStringField(TEXT("doc"), Tooltip.Replace(TEXT("\n"), TEXT(" ")));
		}

		return Field;
	}

	/** The same test Folio applies when it mounts an object: what a view model marks as published. */
	bool IsPublished(const FProperty& Property)
	{
		return Property.HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_Edit)
			&& !Property.IsA<FMapProperty>() && !Property.IsA<FSetProperty>();
	}

	TSharedRef<FJsonObject> DescribeStruct(const UScriptStruct& Struct)
	{
		TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(&Struct); It; ++It)
		{
			// A row's members are flat in a document, so a nested container has nothing to bind as.
			if (!It->IsA<FArrayProperty>() && !It->IsA<FMapProperty>() && !It->IsA<FSetProperty>())
			{
				Fields->SetObjectField(It->GetName(), DescribeScalar(**It));
			}
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetObjectField(TEXT("fields"), Fields);
		return Out;
	}

	/** A command's parameter as the schema writes it, or empty when a document could not pass it. */
	FString DescribeParameter(const FProperty& Property)
	{
		if (Property.HasAnyPropertyFlags(CPF_OutParm) && !Property.HasAnyPropertyFlags(CPF_ConstParm))
		{
			return FString();
		}

		FString Type;
		if (Property.IsA<FBoolProperty>())
		{
			Type = TEXT("bool");
		}
		else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			Type = Numeric->IsFloatingPoint() ? TEXT("float") : TEXT("int");
		}
		else if (Property.IsA<FStrProperty>())
		{
			Type = TEXT("string");
		}
		else if (Property.IsA<FNameProperty>())
		{
			Type = TEXT("name");
		}

		return Type.IsEmpty() ? FString() : Type + TEXT(" ") + Property.GetName();
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Parsed;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed);
		}
		return Parsed;
	}

	TSharedRef<FJsonObject> DescribeClass(const FString& ModelName, const UClass& Class, const FString& UiRoot,
		const TSharedPtr<FJsonObject>& Previous)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("model"), ModelName);
		Schema->SetStringField(TEXT("class"), Class.GetPrefixCPP() + Class.GetName());
		Schema->SetStringField(TEXT("source"), TEXT("reflected"));

		// The documents a schema is checked against are Studio's to record, so they are carried over.
		const TArray<TSharedPtr<FJsonValue>>* Documents = nullptr;
		if (Previous.IsValid() && Previous->TryGetArrayField(TEXT("documents"), Documents))
		{
			Schema->SetArrayField(TEXT("documents"), *Documents);
		}

		// A sample is the one thing the class cannot say. One already chosen is kept, and failing that
		// the value the model's mock has been previewing with.
		const TSharedPtr<FJsonObject>* PreviousFields = nullptr;
		if (Previous.IsValid())
		{
			Previous->TryGetObjectField(TEXT("fields"), PreviousFields);
		}
		const TSharedPtr<FJsonObject> Mock = LoadJson(UiRoot / TEXT("mock") / (ModelName + TEXT(".json")));

		TSharedRef<FJsonObject> Structs = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();

		for (const FProperty* Property = Class.PropertyLink; Property; Property = Property->PropertyLinkNext)
		{
			if (!IsPublished(*Property))
			{
				continue;
			}

			TSharedPtr<FJsonObject> Field;
			if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
			{
				Field = MakeShared<FJsonObject>();
				Field->SetStringField(TEXT("type"), TEXT("array"));

				if (const FStructProperty* Inner = CastField<FStructProperty>(Array->Inner))
				{
					const FString StructName = Inner->Struct->GetStructCPPName();
					Field->SetStringField(TEXT("of"), StructName);
					Structs->SetObjectField(StructName, DescribeStruct(*Inner->Struct));
				}
				else
				{
					const FString Type = ScalarType(*Array->Inner);
					Field->SetStringField(TEXT("of"), Type.IsEmpty() ? TEXT("string") : Type);
				}
			}
			else
			{
				Field = DescribeScalar(*Property);
			}

			if (!Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst))
			{
				Field->SetBoolField(TEXT("writable"), true);
			}

			const TSharedPtr<FJsonObject>* PreviousField = nullptr;
			TSharedPtr<FJsonValue> Sample;
			if (PreviousFields && (*PreviousFields)->TryGetObjectField(Property->GetName(), PreviousField))
			{
				Sample = (*PreviousField)->TryGetField(TEXT("sample"));
			}
			if (!Sample.IsValid() && Mock.IsValid())
			{
				Sample = Mock->TryGetField(Property->GetName());
			}
			if (Sample.IsValid())
			{
				Field->SetField(TEXT("sample"), Sample);
			}

			Fields->SetObjectField(Property->GetName(), Field);
		}

		TSharedRef<FJsonObject> Commands = MakeShared<FJsonObject>();
		for (TFieldIterator<UFunction> It(&Class); It; ++It)
		{
			const UFunction* Function = *It;
			// What Folio binds as a command: callable, returning nothing, taking only what a document can pass.
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) || Function->GetReturnProperty()
				|| Function->GetOwnerClass() == UObject::StaticClass())
			{
				continue;
			}

			TArray<TSharedPtr<FJsonValue>> Parameters;
			bool bCallable = true;
			for (TFieldIterator<FProperty> Parameter(Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter)
			{
				const FString Described = DescribeParameter(**Parameter);
				if (Described.IsEmpty())
				{
					bCallable = false;
					break;
				}
				Parameters.Add(MakeShared<FJsonValueString>(Described));
			}

			if (bCallable)
			{
				Commands->SetArrayField(Function->GetName(), Parameters);
			}
		}

		if (Structs->Values.Num() > 0)
		{
			Schema->SetObjectField(TEXT("structs"), Structs);
		}
		Schema->SetObjectField(TEXT("fields"), Fields);
		if (Commands->Values.Num() > 0)
		{
			Schema->SetObjectField(TEXT("commands"), Commands);
		}

		return Schema;
	}

	void NotifyExport(const FText& Message, const bool bSucceeded)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

bool FolioSchemaExport::HasBoundViewModels()
{
	return FFolioModule::IsAvailable() && FFolioModule::Get().GetBoundViewModels().Num() > 0;
}

void FolioSchemaExport::ExportViewModelSchemas()
{
	const FString UiRoot = FFolioModule::Get().GetUiRoot();
	const FString Directory = UiRoot / TEXT("schema");
	FMessageLog Log(FolioMessageLogName);

	int32 Written = 0;
	for (const TPair<FString, TWeakObjectPtr<UClass>>& Bound : FFolioModule::Get().GetBoundViewModels())
	{
		const UClass* Class = Bound.Value.Get();
		if (!Class)
		{
			continue;
		}

		const FString Path = Directory / (Bound.Key + TEXT(".json"));
		const TSharedRef<FJsonObject> Schema = DescribeClass(Bound.Key, *Class, UiRoot, LoadJson(Path));

		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Schema, Writer);
		Text += TEXT("\n");

		if (FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			++Written;
			Log.Info(FText::Format(LOCTEXT("SchemaWritten", "Wrote the '{0}' schema from {1} to {2}."),
				FText::FromString(Bound.Key), FText::FromString(Class->GetPrefixCPP() + Class->GetName()),
				FText::FromString(Path)));
		}
		else
		{
			Log.Error(FText::Format(LOCTEXT("SchemaFailed", "Could not write '{0}'."), FText::FromString(Path)));
		}
	}

	NotifyExport(FText::Format(LOCTEXT("SchemasExported", "Wrote {0} view model {0}|plural(one=schema,other=schemas) to UI/schema."),
		FText::AsNumber(Written)), Written > 0);
}

// The same export as the toolbar entry, for a script or a commandlet that has no toolbar to click.
static FAutoConsoleCommand GFolioExportViewModelSchemas(
	TEXT("folio.ExportViewModelSchemas"),
	TEXT("Writes UI/schema/<model>.json for every class bound as a view model this session."),
	FConsoleCommandDelegate::CreateStatic(&FolioSchemaExport::ExportViewModelSchemas));

#undef LOCTEXT_NAMESPACE

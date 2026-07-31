#include "FolioDocumentPathCustomization.h"
#include "FolioWorldDocumentCustomization.h"

#include "FolioDocumentPath.h"

#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "Folio"

/**
 * Editor-only. Everything here replaces how something is drawn in the details panel, which is why
 * none of it can live in the runtime module.
 */
class FFolioEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditor =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

		PropertyEditor.RegisterCustomPropertyTypeLayout(
			TEXT("FolioDocumentPath"),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(
				&FFolioDocumentPathCustomization::MakeInstance));

		PropertyEditor.RegisterCustomClassLayout(
			TEXT("FolioWorldDocument"),
			FOnGetDetailCustomizationInstance::CreateStatic(
				&FFolioWorldDocumentCustomization::MakeInstance));

		PropertyEditor.NotifyCustomizationModuleChanged();

		// A listing of its own, so a missing document is a clickable entry in the Message Log rather
		// than a line in the output log that names an asset you then have to go and find. Registering
		// it is only about the label and the window; writing to an unregistered listing works.
		FMessageLogModule& MessageLog =
			FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));

		FMessageLogInitializationOptions Options;
		Options.bShowFilters = true;
		Options.bShowPages = false;
		Options.bAllowClear = true;

		MessageLog.RegisterLogListing(FolioMessageLogName, LOCTEXT("FolioLog", "Folio"), Options);
	}

	virtual void ShutdownModule() override
	{
		if (FPropertyEditorModule* PropertyEditor =
			FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyEditor->UnregisterCustomPropertyTypeLayout(TEXT("FolioDocumentPath"));
			PropertyEditor->UnregisterCustomClassLayout(TEXT("FolioWorldDocument"));
			PropertyEditor->NotifyCustomizationModuleChanged();
		}

		if (FMessageLogModule* MessageLog = FModuleManager::GetModulePtr<FMessageLogModule>(TEXT("MessageLog")))
		{
			MessageLog->UnregisterLogListing(FolioMessageLogName);
		}
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFolioEditorModule, FolioEditor)

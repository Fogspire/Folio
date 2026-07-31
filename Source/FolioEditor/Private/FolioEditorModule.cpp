#include "FolioDocumentPathCustomization.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

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

		PropertyEditor.NotifyCustomizationModuleChanged();
	}

	virtual void ShutdownModule() override
	{
		if (FPropertyEditorModule* PropertyEditor =
			FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyEditor->UnregisterCustomPropertyTypeLayout(TEXT("FolioDocumentPath"));
			PropertyEditor->NotifyCustomizationModuleChanged();
		}
	}
};

IMPLEMENT_MODULE(FFolioEditorModule, FolioEditor)

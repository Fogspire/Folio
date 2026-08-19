#include "FolioDocumentPathCustomization.h"
#include "FolioWorldDocumentCustomization.h"

#include "FolioDocumentPath.h"
#include "FolioEditorStyle.h"
#include "FolioLevelActions.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/CoreDelegates.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"

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

		FFolioEditorStyle::Register();

		// After startup, so the warning is not lost behind everything else the editor prints on the
		// way up, and so the settings it reads have been loaded.
		FCoreDelegates::OnPostEngineInit.AddStatic(&FolioLevelActions::CheckStagedDirectories);

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFolioEditorModule::RegisterMenus));
	}

	/**
	 * The toolbar button, beside the other plugin ones.
	 *
	 * A combo button with a single item rather than a plain button: what belongs here grows, and a
	 * menu that gains a second entry is less disruptive than a button that turns into a menu.
	 */
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
		if (!ToolBar)
		{
			return;
		}

		FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
			TEXT("FolioMenu"),
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FFolioEditorModule::BuildMenu),
			LOCTEXT("FolioToolbar", "Folio"),
			LOCTEXT("FolioToolbarTip", "Folio tools for this level."),
			FSlateIcon(FFolioEditorStyle::GetStyleSetName(), FFolioEditorStyle::GetToolbarIconName()));

		// The style that gives a toolbar button its label beside the icon.
		Entry.StyleNameOverride = TEXT("CalloutToolbar");

		ToolBar->FindOrAddSection(TEXT("Play")).AddEntry(Entry);
	}

	TSharedRef<SWidget> BuildMenu()
	{
		FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);

		Menu.BeginSection(TEXT("FolioLevel"), LOCTEXT("FolioLevelSection", "This Level"));
		Menu.AddMenuEntry(
			LOCTEXT("SelectBroken", "Select Actors With Broken Documents"),
			LOCTEXT("SelectBrokenTip",
				"Selects every actor holding a document component whose path names nothing, or that has "
				"no path at all. One edit in the details panel then fixes the whole set."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FolioLevelActions::SelectActorsWithBrokenDocuments)));

		Menu.AddMenuEntry(
			LOCTEXT("StageDocuments", "Stage Documents Used In This Level"),
			LOCTEXT("StageDocumentsTip",
				"Adds whatever directories this level's documents live in to the packaging staging list, "
				"so they are in a packaged build. Writes to DefaultGame.ini."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FolioLevelActions::StageDocumentsUsedInLevel)));
		Menu.EndSection();

		Menu.BeginSection(TEXT("FolioProject"), LOCTEXT("FolioProjectSection", "This Project"));
		Menu.AddMenuEntry(
			LOCTEXT("SetCommonUiCvars", "Set CommonUI Console Variables"),
			LOCTEXT("SetCommonUiCvarsTip",
				"Turns off CommonUI's virtual pointer, which aims the pad at a software cursor rather than "
				"at what is focused. Writes the three lines to DefaultEngine.ini, leaving anything already "
				"there alone, and needs an editor restart. Greyed out in a project without CommonUI."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FolioLevelActions::SetCommonUiConsoleVariables),
				FCanExecuteAction::CreateStatic(&FolioLevelActions::HasCommonUI)));
		Menu.EndSection();

		return Menu.MakeWidget();
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

		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FFolioEditorStyle::Unregister();
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFolioEditorModule, FolioEditor)

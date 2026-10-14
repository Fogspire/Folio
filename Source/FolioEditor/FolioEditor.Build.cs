using UnrealBuildTool;

public class FolioEditor : ModuleRules
{
	public FolioEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			// The whole reason this module exists: a property type customisation is the only way to
			// replace what a struct field draws as.
			"PropertyEditor",
			"EditorStyle",
			// The Message Log listing a missing document is reported into.
			"MessageLog",
			// The staging list, read at startup to warn about a UI tree that will not be packaged.
			"DeveloperToolSettings",
			// Checking DefaultEngine.ini out before writing the CommonUI variables to it.
			"SourceControl",
			// The toolbar entry, which is a ToolMenus extension rather than a Slate widget.
			"ToolMenus",
			// The view model schemas written out for Folio Studio.
			"Json",
			"Projects",
			"Folio",
			// FolioModule.h reaches RmlUi types through FolioRmlUi.h, so anything including it needs
			// the library on its own include path.
			"RmlUiLibrary",
			// And the shared core, for the input state FolioModule.h carries.
			"FolioCore",
		});
	}
}

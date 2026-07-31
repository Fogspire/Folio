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
			"Projects",
			"Folio",
		});
	}
}

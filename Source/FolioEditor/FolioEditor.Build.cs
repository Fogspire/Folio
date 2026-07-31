using System.IO;
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

		// Folio's public headers include private ones - FolioWorldDocument.h reaches FolioDrawList.h,
		// SFolioCanvas.h reaches FolioNavigation.h - so a module cannot compile against them without
		// this. It is a workaround for a layout problem in Folio, not a design; see docs/known_gaps.md.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "Folio", "Private"));

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
			// FolioModule.h reaches RmlUi types through FolioRmlUi.h, so anything including it needs
			// the library on its own include path.
			"RmlUiLibrary",
			// And the shared core, for the input state FolioModule.h carries.
			"FolioCore",
		});
	}
}

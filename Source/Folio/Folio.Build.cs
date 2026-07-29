using UnrealBuildTool;

public class Folio : ModuleRules
{
	public Folio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// RmlUi is built with exceptions enabled because its dependencies expect them, and it
		// uses std:: containers that assume the same. Matching here keeps the two consistent
		// across the static library boundary.
		bEnableExceptions = true;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"Projects",
			"TraceLog",
			"ApplicationCore",
			"RmlUiLibrary",
			"FolioCore",
		});
	}
}

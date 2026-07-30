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
			// For UFolioWidget alone, which is how a document sits in a game's existing widget
			// tree. Folio builds no UI with UMG; this is the boundary, not a dependency of the
			// renderer or the text engine.
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"Projects",
			"TraceLog",
			"ApplicationCore",
			"Json",
			// For the view model bridge. This is the engine's own field notification interface, not
			// the ModelViewViewModel plugin: a UMVVMViewModelBase implements it, so Folio binds view
			// models without depending on MVVM and a project ignoring MVVM pays nothing.
			"FieldNotification",
			"RmlUiLibrary",
			"FolioCore",
		});
	}
}

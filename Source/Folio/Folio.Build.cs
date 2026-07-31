using System.IO;
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
			// For the screen-pass helpers the after-tonemap pass uses: FScreenPassTexture,
			// AddDrawTexturePass and the system textures. RDG and RHI alone are enough for the layer
			// path, which is why this only became necessary with world-space documents.
			"Renderer",
			"Projects",
			// For UFolioSettings, which is where a project sets a default once rather than per document.
			"DeveloperSettings",
			// For writing a surface render to a PNG, which is how the engine's output becomes
			// comparable with the preview tool's.
			"ImageWrapper",
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

		// FPostProcessingInputs is what PrePostProcessPass_RenderThread receives, and the in-scene draw
		// mode cannot be implemented without reading it. It lives in the renderer's Internal folder, which
		// UBT does not put on a dependent module's include path, so the path is added by hand.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Renderer", "Internal"));

		// FMobilePostProcessingInputs, which the mobile renderer hands to its own
		// PrePostProcessPassMobile_RenderThread hook, is declared only here. The deferred twin is
		// in Internal; this one was never promoted.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Renderer", "Private"));

		// Hot reload only. DirectoryWatcher is a Developer module and does not exist in a game
		// build, which is what the -uidev loose-file override covers instead.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("DirectoryWatcher");

			// The staging list, so a document that will not be in a packaged build can be reported
			// while there is still someone at a keyboard to fix it.
			PrivateDependencyModuleNames.Add("DeveloperToolSettings");
		}
	}
}

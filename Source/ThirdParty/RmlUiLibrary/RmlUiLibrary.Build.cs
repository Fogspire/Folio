using System.IO;
using UnrealBuildTool;

/// <summary>
/// RmlUi 6.2, built as static libraries by scripts/build_ue_libs.ps1.
///
/// The libraries are not in source control: they run to tens of megabytes and would land in
/// history again on every rebuild. Building them is a one-time step, documented in README.md.
/// </summary>
public class RmlUiLibrary : ModuleRules
{
	public RmlUiLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IncludePath = Path.Combine(ModuleDirectory, "include");
		string LibraryPath = Path.Combine(ModuleDirectory, "lib", "Win64");

		if (!Directory.Exists(IncludePath) || !File.Exists(Path.Combine(LibraryPath, "rmlui.lib")))
		{
			throw new BuildException(
				"RmlUi has not been built for Unreal.\n" +
				"Run: Plugins\\Folio\\scripts\\build_ue_libs.ps1\n" +
				"It needs Visual Studio 2022 and takes a few minutes on a cold tree.");
		}

		PublicSystemIncludePaths.Add(IncludePath);

		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rmlui.lib"));
		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "rmlui_debugger.lib"));

		// Without this every RmlUi symbol is declared __declspec(dllimport) and nothing links.
		PublicDefinitions.Add("RMLUI_STATIC_LIB=1");

		// RmlUi's font engine is FreeType, and it is deliberately the engine's copy rather
		// than one of our own: two FreeType builds in one link is a duplicate-symbol failure
		// at best, and silent version-skewed aliasing at worst, since Slate already links it.
		//
		// zlib and libPNG come along because the engine's FreeType is built with embedded
		// bitmap and compressed table support, so freetype.lib references them whether or not
		// RmlUi ever loads such a font. FreeType2.Build.cs declares them privately, which does
		// not reach the module that actually links, so they are named again here.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2", "zlib", "UElibPNG");
	}
}

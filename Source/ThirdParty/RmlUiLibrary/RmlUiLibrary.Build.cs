using System.IO;
using UnrealBuildTool;

/// <summary>
/// RmlUi 6.2, built as static libraries by scripts/build_ue_libs.sh.
///
/// The libraries are not in source control: they run to tens of megabytes and would land in
/// history again on every rebuild. Building them is a one-time step, documented in README.md.
/// </summary>
public class RmlUiLibrary : ModuleRules
{
	/// <summary>Subdirectory of lib/ holding the build for the target platform.</summary>
	protected virtual string LibraryPlatform => "Win64";

	/// <summary>
	/// Directory the static libraries are read from. A platform whose libraries are not
	/// distributed with the plugin overrides this to point outside it.
	/// </summary>
	protected virtual string LibraryDirectory => Path.Combine(ModuleDirectory, "lib", LibraryPlatform);

	/// <summary>File name of a static library, which is not spelled the same way on every platform.</summary>
	protected virtual string StaticLibrary(string BaseName) => BaseName + ".lib";

	/// <summary>How to produce the libraries, quoted back when the build is missing.</summary>
	protected virtual string BuildInstructions =>
		"Run: Plugins/Folio/scripts/build_ue_libs.sh\n" +
		"It needs Visual Studio 2022 and takes a few minutes on a cold tree.";

	public RmlUiLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IncludePath = Path.Combine(ModuleDirectory, "include");
		string LibraryPath = LibraryDirectory;

		if (!Directory.Exists(IncludePath) || !File.Exists(Path.Combine(LibraryPath, StaticLibrary("rmlui"))))
		{
			throw new BuildException(
				"RmlUi has not been built for " + LibraryPlatform + ".\n" +
				"Expected: " + LibraryPath + "\n" +
				BuildInstructions);
		}

		PublicSystemIncludePaths.Add(IncludePath);

		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, StaticLibrary("rmlui")));
		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, StaticLibrary("rmlui_debugger")));

		// Without this every RmlUi symbol is declared __declspec(dllimport) and nothing links.
		PublicDefinitions.Add("RMLUI_STATIC_LIB=1");

		// RmlUi is built with its own type system because Unreal compiles without RTTI. This
		// must match the library exactly: the header selects a different rmlui_dynamic_cast
		// depending on it, so a mismatch is an ODR violation that only shows up as a crash
		// inside RmlUi at runtime.
		PublicDefinitions.Add("RMLUI_CUSTOM_RTTI");

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

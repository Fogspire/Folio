using System.IO;
using UnrealBuildTool;

/// <summary>
/// msdfgen 1.12, built as static libraries by scripts/build_ue_libs.sh alongside RmlUi.
///
/// The libraries are not in source control, for the same reason RmlUi's are not.
/// </summary>
public class MsdfgenLibrary : ModuleRules
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
		"It builds RmlUi and msdfgen together.";

	public MsdfgenLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string IncludePath = Path.Combine(ModuleDirectory, "include");
		string LibraryPath = LibraryDirectory;

		if (!Directory.Exists(IncludePath) || !File.Exists(Path.Combine(LibraryPath, StaticLibrary("msdfgen-core"))))
		{
			throw new BuildException(
				"msdfgen has not been built for " + LibraryPlatform + ".\n" +
				"Expected: " + LibraryPath + "\n" +
				BuildInstructions);
		}

		PublicSystemIncludePaths.Add(IncludePath);

		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, StaticLibrary("msdfgen-core")));
		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, StaticLibrary("msdfgen-ext")));

		// Skia's path operations, vendored as a single-file amalgam under
		// Source/ThirdParty/SkiaSimplify. msdfgen's resolveShapeGeometry lives in the extensions
		// library but its implementation is here, and without it every heavy font weight renders
		// with broken stems.
		PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, StaticLibrary("skia")));

		// These are PUBLIC definitions on the CMake targets, so a consumer that disagrees with
		// the library gets a different declaration of the same function: an ODR violation, not a
		// compile error. cmake/Pins.cmake's folio_set_msdfgen_options() is the other half.
		PublicDefinitions.Add("MSDFGEN_USE_CPP11");
		PublicDefinitions.Add("MSDFGEN_PUBLIC=");
		PublicDefinitions.Add("MSDFGEN_EXT_PUBLIC=");
		PublicDefinitions.Add("MSDFGEN_EXTENSIONS");
		PublicDefinitions.Add("MSDFGEN_DISABLE_SVG");
		PublicDefinitions.Add("MSDFGEN_DISABLE_PNG");
		PublicDefinitions.Add("MSDFGEN_DISABLE_VARIABLE_FONTS");
		PublicDefinitions.Add("MSDFGEN_USE_SKIA");

		// Slate compiles its own copy of msdfgen into SlateCore, but wraps it in a parent
		// namespace (SlateSdfGenerator.cpp defines MSDFGEN_PARENT_NAMESPACE=SlateMsdfgen before
		// including msdfgen.cpp), so its symbols are SlateMsdfgen::msdfgen::* and ours are
		// msdfgen::*. Nothing collides in a monolithic link. Re-check this on an engine upgrade:
		// if Slate ever drops that define, both copies land in one namespace and the packaged
		// build fails with LNK2005 while editor builds keep working.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2", "zlib", "UElibPNG");
	}
}

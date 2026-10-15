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
	protected virtual string LibraryPlatform =>
		Target.Platform == UnrealTargetPlatform.Mac ? "Mac"
		: Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) ? "Linux"
		: "Win64";

	/// <summary>
	/// Directory the static libraries are read from. A platform whose libraries are not
	/// distributed with the plugin overrides this to point outside it.
	/// </summary>
	protected virtual string LibraryDirectory => Path.Combine(ModuleDirectory, "lib", LibraryPlatform);

	/// <summary>File name of a static library, which is not spelled the same way on every platform.</summary>
	protected virtual string StaticLibrary(string BaseName) =>
		Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) ? BaseName + ".lib" : "lib" + BaseName + ".a";

	/// <summary>How to produce the libraries, quoted back when the build is missing.</summary>
	protected virtual string BuildInstructions =>
		"Run: Plugins/Folio/scripts/build_ue_libs.sh\n" +
		"It needs Visual Studio 2022 on Windows, Xcode, CMake and Ninja on a Mac, or the engine's\n" +
		"Linux toolchain, CMake and Ninja on Linux, and takes a few minutes on a cold tree.";

	/// <summary>The patches Folio applies to RmlUi, which the libraries must have been built with.</summary>
	private string PatchDirectory => Path.Combine(ModuleDirectory, "..", "..", "..", "patches", "RmlUi");

	/// <summary>
	/// Names what differs between the patches the libraries were built with, recorded by build_ue_libs.sh
	/// as RMLUI_PATCHES beside them, and the patches in the tree; null when they match. Only libraries
	/// built inside the plugin are checked, since a platform that builds them elsewhere stamps them its own way.
	/// </summary>
	private string StalePatches(string LibraryPath)
	{
		if (!Path.GetFullPath(LibraryPath).StartsWith(Path.GetFullPath(ModuleDirectory)))
		{
			return null;
		}

		var Expected = new System.Collections.Generic.List<string>();
		if (Directory.Exists(PatchDirectory))
		{
			string[] Patches = Directory.GetFiles(PatchDirectory, "*.patch");
			System.Array.Sort(Patches, System.StringComparer.Ordinal);
			foreach (string Patch in Patches)
			{
				using (var Sha = System.Security.Cryptography.SHA256.Create())
				{
					string Hash = System.BitConverter.ToString(Sha.ComputeHash(File.ReadAllBytes(Patch))).Replace("-", "").ToLowerInvariant();
					Expected.Add(Path.GetFileName(Patch) + " " + Hash);
				}
			}
		}

		string StampPath = Path.Combine(LibraryPath, "RMLUI_PATCHES");
		var Built = new System.Collections.Generic.List<string>();
		if (File.Exists(StampPath))
		{
			foreach (string Line in File.ReadAllLines(StampPath))
			{
				if (Line.Trim().Length > 0)
				{
					Built.Add(Line.Trim());
				}
			}
		}

		foreach (string Line in Expected)
		{
			if (!Built.Contains(Line))
			{
				return "missing or changed: " + Line.Split(' ')[0];
			}
		}
		foreach (string Line in Built)
		{
			if (!Expected.Contains(Line))
			{
				return "no longer in the tree: " + Line.Split(' ')[0];
			}
		}
		return null;
	}

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

		// So a pull that changes the patches re-runs these rules rather than reusing the cached makefile.
		ExternalDependencies.Add(Path.Combine(LibraryPath, "RMLUI_PATCHES"));
		if (Directory.Exists(PatchDirectory))
		{
			ExternalDependencies.Add(PatchDirectory);
			ExternalDependencies.AddRange(Directory.GetFiles(PatchDirectory, "*.patch"));
		}

		string Stale = StalePatches(LibraryPath);
		if (Stale != null)
		{
			throw new BuildException(
				"RmlUi for " + LibraryPlatform + " was built without the patches now in Plugins/Folio/patches/RmlUi (" + Stale + ").\n" +
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

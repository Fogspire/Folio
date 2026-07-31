using UnrealBuildTool;

/// <summary>
/// The MSDF font engine and atlas code, shared verbatim with the standalone preview tool.
///
/// Unreal Build Tool only compiles files under a module directory, so the shared code lives here
/// and the root CMakeLists.txt compiles the same Private/*.cpp into its folio_core target. Both
/// renderers therefore generate byte-identical distance fields.
///
/// Nothing here includes an Unreal header except FolioCoreModule.cpp, which CMake excludes.
/// </summary>
public class FolioCore : ModuleRules
{
	public FolioCore(ReadOnlyTargetRules Target) : base(Target)
	{
		// Unreal's shared PCH force-includes CoreMinimal, whose macros (check, PI, TEXT, min/max)
		// collide with msdfgen and FreeType. Unity builds would bleed them across files the same
		// way. Both are off so these translation units see only their own includes.
		PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;
		bUseUnity = false;

		// Matches RmlUi and Folio across the static library boundary.
		bEnableExceptions = true;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RmlUiLibrary",
			"MsdfgenLibrary",
		});

		// msdfgen's extension library decomposes FT_Outline directly, and the font engine drives
		// FT_Set_Var_Design_Coordinates for variable axes. This is the engine's FreeType, which
		// must be a build with TT_CONFIG_OPTION_GX_VAR_SUPPORT; FolioFontFace probes
		// FT_Get_MM_Var rather than trusting the version number.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2", "zlib", "UElibPNG");

		// The engine's own HarfBuzz rather than a second copy, because it is a C library with no
		// namespace: two builds of it in one monolithic executable is a duplicate symbol for every
		// entry point. The version routing here mirrors HarfBuzz.Build.cs, which only ships a
		// usable library on these platforms and only when FreeType and ICU are both compiled in.
		// ICU comes with it: HarfBuzz gets its Unicode character properties from ICU, so the
		// library does not link without it.
		bool bHarfBuzz = Target.bCompileFreeType && Target.bCompileICU
			&& Target.Type != TargetType.Server
			&& (Target.Platform == UnrealTargetPlatform.IOS
				|| Target.Platform == UnrealTargetPlatform.Mac
				|| Target.IsInPlatformGroup(UnrealPlatformGroup.Windows)
				|| Target.IsInPlatformGroup(UnrealPlatformGroup.Android)
				|| Target.IsInPlatformGroup(UnrealPlatformGroup.Unix));

		if (bHarfBuzz)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "HarfBuzz", "ICU");
		}

		PrivateDefinitions.Add("FOLIO_WITH_HARFBUZZ=" + (bHarfBuzz ? "1" : "0"));
	}
}

#include "FolioModule.h"

#include "FolioInsights.h"

#include "FolioFileInterface.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"
#include "FolioSystemInterface.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogFolio);

#define LOCTEXT_NAMESPACE "Folio"

void FFolioModule::StartupModule()
{
	// Before any face is loaded, so the core's generation scopes are captured from the start.
	FolioInsights::Install();

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Folio"));
	PluginRoot = Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()) : FString();

	// The blur behind box-shadow is a global shader living in the plugin, so its virtual
	// directory has to exist before the shader compiler looks for it.
	if (!PluginRoot.IsEmpty())
	{
		AddShaderSourceDirectoryMapping(TEXT("/Folio"), FPaths::Combine(PluginRoot, TEXT("Shaders/Private")));
	}

	// Documents live in the project rather than the plugin: Folio ships no game UI.
	ContentRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	SystemInterface = MakeUnique<FFolioSystemInterface>();
	FileInterface = MakeUnique<FFolioFileInterface>(ContentRoot, PluginRoot);
	RenderInterface = MakeUnique<FFolioRenderInterface>();

	// Both interfaces have to be installed before Initialise, not after: RmlUi reads files
	// and logs during startup, and the defaults it would otherwise use call fopen.
	Rml::SetSystemInterface(SystemInterface.Get());
	Rml::SetFileInterface(FileInterface.Get());
	Rml::SetRenderInterface(RenderInterface.Get());

	bRmlInitialised = Rml::Initialise();
	if (!bRmlInitialised)
	{
		UE_LOG(LogFolio, Error, TEXT("Rml::Initialise failed. Folio widgets will render nothing."));
		return;
	}

	RegisterFonts();

	UE_LOG(LogFolio, Log, TEXT("Folio ready. Content root: %s, plugin root: %s"), *ContentRoot, *PluginRoot);
}

void FFolioModule::ShutdownModule()
{
	// Anything holding a context drops it while RmlUi is still alive to be told about it.
	PreShutdownDelegate.Broadcast();
	PreShutdownDelegate.Clear();

	if (bRmlInitialised)
	{
		Rml::Shutdown();
		bRmlInitialised = false;
	}

	// Only safe after Shutdown: RmlUi logs during teardown and would call into a destroyed
	// interface otherwise.
	Rml::SetRenderInterface(nullptr);
	Rml::SetFileInterface(nullptr);
	Rml::SetSystemInterface(nullptr);

	RenderInterface.Reset();
	FileInterface.Reset();
	SystemInterface.Reset();
}

void FFolioModule::RegisterFonts()
{
	// No typeface is bundled: the choice is a per-game decision. The engine's Roboto is the
	// default so a document renders text before anything is configured, which is the same
	// resolution order the preview tool uses.
	// Absolute, because font loading goes through our file interface and the engine content
	// directory is relative to the binary. Left relative it resolves against the project's
	// content root instead and every face fails to open.
	const FString FontDir = FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir()) / TEXT("Slate/Fonts");

	static const TCHAR* Faces[] = {
		TEXT("Roboto-Regular.ttf"),
		TEXT("Roboto-Bold.ttf"),
		TEXT("Roboto-Italic.ttf"),
		TEXT("Roboto-BoldItalic.ttf"),
	};

	int32 Loaded = 0;
	for (const TCHAR* Face : Faces)
	{
		const FString Path = FontDir / Face;
		if (!FPaths::FileExists(Path))
		{
			continue;
		}

		if (Rml::LoadFontFace(Folio::ToRmlString(Path)))
		{
			++Loaded;
		}
		else
		{
			UE_LOG(LogFolio, Warning, TEXT("could not register font face '%s'"), *Path);
		}
	}

	if (Loaded == 0)
	{
		UE_LOG(LogFolio, Error,
			TEXT("no font faces registered; RmlUi renders no text at all without one"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFolioModule, Folio)

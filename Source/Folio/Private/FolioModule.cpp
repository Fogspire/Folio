#include "FolioModule.h"

#include "FolioInsights.h"

#include "FolioCore/FolioFilters.h"
#include "FolioCore/FolioFontEffects.h"
#include "FolioCore/FolioFontEngine.h"
#include "FolioFileInterface.h"
#include "FolioCoreHost.h"
#include "FolioFontCache.h"
#include "FolioHotReload.h"
#include "FolioInputPreprocessor.h"
#include "FolioWorldPass.h"
#include "FolioNavigation.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"
#include "FolioSystemInterface.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY(LogFolio);

#define LOCTEXT_NAMESPACE "Folio"

FFolioModule::FFolioModule() = default;
FFolioModule::~FFolioModule() = default;

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

#if !UE_BUILD_SHIPPING
	// Logged loudly rather than quietly: a stale file in the override directory presents as the game
	// ignoring an edit, and there is nothing in the symptom pointing at an override.
	if (const FString Override = FFolioFileInterface::OverrideRootFromCommandLine(); !Override.IsEmpty())
	{
		FileInterface->SetOverrideRoot(Override);
		UE_LOG(LogFolio, Warning, TEXT("-uidev: reading UI from '%s' ahead of the package"), *Override);
	}
#endif
	RenderInterface = MakeUnique<FFolioRenderInterface>();

	// The system and file interfaces go in first, before anything that might log or read: the
	// font engine is built next and would otherwise report a failure to RmlUi's default sink
	// rather than to the log, which is a silence that takes a while to explain.
	Rml::SetSystemInterface(SystemInterface.Get());
	Rml::SetFileInterface(FileInterface.Get());
	Rml::SetRenderInterface(RenderInterface.Get());

	// The core reaches files and the log through here rather than RmlUi's globals, which this
	// module and FolioCore do not share.
	CoreHost = MakeUnique<FFolioCoreHost>(*FileInterface);
	Folio::SetHost(CoreHost.Get());

	// The render interface is also the atlas sink: the font engine builds its pages through it,
	// so a page arrives back at RenderGeometry looking like an ordinary texture and is recognised
	// there. RmlUi never learns the difference.
	FontEngine = MakeUnique<Folio::FFolioFontEngine>(*RenderInterface);
	if (!FontEngine->IsValid())
	{
		UE_LOG(LogFolio, Error,
			TEXT("FreeType would not start, so no font face can load and no text will render"));
	}

	FontCacheDir = FPaths::ProjectSavedDir() / TEXT("Folio") / TEXT("Fields");
	FontCache = MakeUnique<FFolioFontCache>(FontCacheDir);
	FontEngine->SetCacheStore(FontCache.Get());

	// RmlUi is built with RMLUI_FONT_ENGINE=none, so a missing font engine fails Initialise
	// outright rather than leaving a context that silently renders no text.
	Rml::SetFontEngineInterface(FontEngine.Get());

	bRmlInitialised = Rml::Initialise();
	if (!bRmlInitialised)
	{
		UE_LOG(LogFolio, Error, TEXT("Rml::Initialise failed. Folio widgets will render nothing."));
		return;
	}

	// After Initialise, because registering over a name replaces an instancer the factory only
	// creates during it. Markup is unchanged; the effects stop being baked into glyph textures and
	// start being resolved from the field.
	Folio::RegisterFolioFontEffects();
	Folio::RegisterFolioFilters();

	RegisterFonts();

	// Ahead of CommonUI's, so a focused document actually receives the gamepad.
	FFolioInputPreprocessor::Register();

#if WITH_EDITOR
	// Deferred: this module loads at PostConfigInit and DirectoryWatcher is not reliably loadable
	// that early.
	HotReload = MakeUnique<FFolioHotReload>();
#endif

	// Also deferred, and for a harder reason: registering a scene view extension asserts on GEngine,
	// which does not exist at PostConfigInit. Registered unconditionally once it does, and inert until a
	// document exists, because an extension cannot be added mid-frame.
	FCoreDelegates::GetOnPostEngineInit().AddLambda([this]
	{
		WorldPass = FSceneViewExtensions::NewExtension<FFolioWorldPass>();

#if WITH_EDITOR
		HotReload->Start();
#endif
	});

	UE_LOG(LogFolio, Log, TEXT("Folio ready. Content root: %s, plugin root: %s"), *ContentRoot, *PluginRoot);
}

void FFolioModule::ShutdownModule()
{
	// Dropped before RmlUi goes, since the pass reads recordings that belong to documents.
	WorldPass.Reset();

#if WITH_EDITOR
	HotReload.Reset();
#endif

	FFolioInputPreprocessor::Unregister();

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

	// RmlUi holds raw pointers to the instancers, so they only go once it has shut down.
	Folio::ReleaseFolioFontEffects();
	Folio::ReleaseFolioFilters();

	RenderInterface.Reset();
	Folio::SetHost(nullptr);
	CoreHost.Reset();
	FontEngine.Reset();
	FontCache.Reset();
	FileInterface.Reset();
	SystemInterface.Reset();
}

void FFolioModule::RegisterFonts()
{
	// The same order the preview tool uses, so a document looks the same in both: the game's own
	// faces first, then the ones Folio ships, then the engine's Roboto as a last resort. Anything
	// the game puts under ui/fonts wins outright.
	//
	// Paths are absolute because font loading goes through our file interface, and both the engine
	// and plugin directories are relative to the binary rather than the content root; left
	// relative, every face fails to open.
	int32 Loaded = 0;

	const auto LoadFacesIn = [&Loaded](const FString& Directory)
	{
		if (Directory.IsEmpty() || !FPaths::DirectoryExists(Directory))
		{
			return;
		}

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.ttf")), true, false);
		IFileManager::Get().FindFiles(Files, *(Directory / TEXT("*.otf")), true, false);
		Files.Sort();

		for (const FString& File : Files)
		{
			// The first face registered is the fallback. A document naming a family nobody
			// registered would otherwise render nothing at all, which reads as a layout bug and
			// says nothing about the cause.
			if (Rml::LoadFontFace(Folio::ToRmlString(Directory / File), Loaded == 0))
			{
				++Loaded;
			}
			else
			{
				UE_LOG(LogFolio, Warning, TEXT("could not register font face '%s'"), *File);
			}
		}
	};

	LoadFacesIn(ContentRoot / TEXT("ui") / TEXT("fonts"));

	if (Loaded == 0)
	{
		LoadFacesIn(PluginRoot / TEXT("fonts"));
	}

	if (Loaded == 0)
	{
		LoadFacesIn(FPaths::ConvertRelativePathToFull(FPaths::EngineContentDir()) / TEXT("Slate/Fonts"));
	}

	if (Loaded == 0)
	{
		UE_LOG(LogFolio, Error,
			TEXT("no font faces registered; RmlUi renders no text at all without one"));
	}
	else
	{
		UE_LOG(LogFolio, Log, TEXT("registered %d font face(s)"), Loaded);
	}
}

void FFolioModule::SetInputDevice(const Folio::EInputDevice Device)
{
	if (Device == InputDevice || Device == Folio::EInputDevice::Unknown)
	{
		return;
	}

	InputDevice = Device;
	UE_LOG(LogFolio, Verbose, TEXT("input device is now %s"), Folio::DescribeDevice(Device));
	InputDeviceDelegate.Broadcast(Device);
}

Folio::EConfirmButton FFolioModule::GetConfirmButton() const
{
	return Folio::ConfirmButton();
}

void FFolioModule::SetConfirmButton(const Folio::EConfirmButton Button)
{
	Folio::SetConfirmButton(Button);
}

FSimpleMulticastDelegate& FFolioModule::OnConfirmButtonChanged()
{
	return Folio::OnConfirmButtonChanged();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFolioModule, Folio)

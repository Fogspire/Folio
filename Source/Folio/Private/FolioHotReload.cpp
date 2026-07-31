#include "FolioHotReload.h"

#if WITH_EDITOR

#include "FolioFileInterface.h"
#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"
#include "FolioWorldDocument.h"
#include "SFolioCanvas.h"

#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Misc/Paths.h"

#include <RmlUi/Core/Core.h>

namespace
{
	TAutoConsoleVariable<bool> CVarHotReload(
		TEXT("folio.HotReload"),
		true,
		TEXT("Watch the UI source tree and reload open documents when a file changes. Off stops the ")
		TEXT("watcher outright, which is what a profiling run wants."),
		ECVF_Default);

	/** What a change to this file means for an open document. */
	enum class EChangeKind : uint8
	{
		Irrelevant,
		Markup,
		Style,
		Asset,
	};

	EChangeKind ClassifyChange(const FString& Path)
	{
		const FString Extension = FPaths::GetExtension(Path);

		if (Extension == TEXT("rml"))
		{
			return EChangeKind::Markup;
		}
		if (Extension == TEXT("rcss"))
		{
			return EChangeKind::Style;
		}

		// Anything else RmlUi opened is art, and there is no list of image extensions to keep in
		// step with: if it was opened and it is not markup or style, releasing it is the answer.
		return EChangeKind::Asset;
	}
}

FFolioHotReload::FFolioHotReload()
{
	if (IConsoleVariable* Variable = CVarHotReload.AsVariable())
	{
		Variable->SetOnChangedCallback(FConsoleVariableDelegate::CreateLambda(
			[this](IConsoleVariable* Changed)
			{
				if (Changed->GetBool())
				{
					Start();
				}
				else
				{
					Stop();
				}
			}));
	}
}

FFolioHotReload::~FFolioHotReload()
{
	// The console variable is a file static and outlives the module, so a callback capturing this
	// would be called on a freed object by anyone setting folio.HotReload after an unload.
	if (IConsoleVariable* Variable = CVarHotReload.AsVariable())
	{
		Variable->SetOnChangedCallback(FConsoleVariableDelegate());
	}

	Stop();
}

void FFolioHotReload::Start()
{
	if (IsRunning() || !CVarHotReload.GetValueOnGameThread())
	{
		return;
	}

	FDirectoryWatcherModule& Module =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));

	IDirectoryWatcher* Watcher = Module.Get();
	if (!Watcher)
	{
		return;
	}

	const FFolioModule& Folio = FFolioModule::Get();

	// The project's own UI tree and Folio's shipped stylesheets. Fonts are deliberately not watched:
	// a face change means re-registering it and rebuilding its field cache, which is not a reload.
	TArray<FString> Roots;
	Roots.Add(Folio.GetUiRoot());
	if (!Folio.GetPluginRoot().IsEmpty())
	{
		Roots.Add(Folio.GetPluginRoot() / TEXT("rcss"));

		// Folio's own dev documents. They live outside rcss/ because rcss/ is what a packaged build
		// stages, and watching only the staged tree left the one set of documents most likely to be
		// edited while a session is running unable to hot reload at all.
		Roots.Add(Folio.GetPluginRoot() / TEXT("dev"));
	}

	for (const FString& Root : Roots)
	{
		if (!FPaths::DirectoryExists(Root))
		{
			continue;
		}

		FWatch Watch;
		Watch.Directory = Root;

		if (Watcher->RegisterDirectoryChangedCallback_Handle(
			Root,
			IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FFolioHotReload::OnDirectoryChanged),
			Watch.Handle))
		{
			Watches.Add(MoveTemp(Watch));
		}
	}

	if (IsRunning())
	{
		UE_LOG(LogFolio, Log, TEXT("hot reload watching %d director(ies); 'folio.HotReload 0' stops it"),
			Watches.Num());
	}
}

void FFolioHotReload::Stop()
{
	if (FlushHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FlushHandle);
		FlushHandle.Reset();
	}
	Pending.Empty();

	if (FDirectoryWatcherModule* Module =
		FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = Module->Get())
		{
			for (FWatch& Watch : Watches)
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(Watch.Directory, Watch.Handle);
			}
		}
	}

	Watches.Empty();
}

void FFolioHotReload::OnDirectoryChanged(const TArray<FFileChangeData>& Changes)
{
	for (const FFileChangeData& Change : Changes)
	{
		if (Change.Action != FFileChangeData::FCA_Removed)
		{
			Pending.Add(FFolioFileInterface::NormalisePath(Change.Filename));
		}
	}

	if (Pending.Num() == 0 || FlushHandle.IsValid())
	{
		return;
	}

	FlushHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FFolioHotReload::Flush), 0.0f);
}

bool FFolioHotReload::Flush(float)
{
	FlushHandle.Reset();

	const TSet<FString> Changed = MoveTemp(Pending);
	Pending.Empty();

	FFolioModule& Folio = FFolioModule::Get();
	const FFolioFileInterface* Files = Folio.GetFileInterface();
	if (!Files)
	{
		return false;
	}

	const TSet<FString>& Dependencies = Files->GetOpened();

	bool bMarkup = false;
	bool bStyle = false;
	bool bAsset = false;

	for (const FString& Path : Changed)
	{
		// Only files a document actually pulled in. Without this, saving anything anywhere under
		// UI/ reloads every open document, including the mock data and notes nothing reads.
		if (!Dependencies.Contains(Path))
		{
			continue;
		}

		switch (ClassifyChange(Path))
		{
		case EChangeKind::Markup: bMarkup = true; break;
		case EChangeKind::Style:  bStyle = true; break;
		case EChangeKind::Asset:  bAsset = true; break;
		default: break;
		}
	}

	if (!bMarkup && !bStyle && !bAsset)
	{
		return false;
	}

	// A copy: reloading a document can destroy a canvas, and the live set is what would be iterated.
	const TArray<SFolioCanvas*> Canvases = SFolioCanvas::GetLiveCanvases();

	if (bAsset && !bMarkup)
	{
		// Named rather than wholesale, so the distance field pages the font engine owns are not
		// dropped along with the art.
		if (FFolioRenderInterface* Renderer = Folio.GetRenderInterface())
		{
			Renderer->ReloadChangedTextures();
		}
		Rml::ReleaseCompiledGeometry();
	}

	for (SFolioCanvas* Canvas : Canvases)
	{
		if (bMarkup)
		{
			Canvas->ReloadDocument();
		}
		else if (bStyle)
		{
			Canvas->ReloadStyleSheets();
		}
	}

	// A copy for the same reason, and dirtied on an art change where a canvas needs nothing: a world
	// document caches its recording, so releasing the geometry behind it is not enough to redraw it.
	for (UFolioWorldDocument* Surface : TArray<UFolioWorldDocument*>(UFolioWorldDocument::GetLive()))
	{
		if (bMarkup)
		{
			Surface->ReloadDocument();
		}
		else if (bStyle)
		{
			Surface->ReloadStyleSheets();
		}
		else
		{
			Surface->MarkDirty();
		}
	}

	UE_LOG(LogFolio, Log, TEXT("hot reload: %s"),
		bMarkup ? TEXT("rebuilt the open documents") : bStyle ? TEXT("reloaded stylesheets") : TEXT("released changed art"));

	return false;
}

#endif

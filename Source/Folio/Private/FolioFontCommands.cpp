#include "FolioCore/FolioFontEngine.h"
#include "FolioModule.h"
#include "FolioWorldDocument.h"
#include "SFolioCanvas.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

/**
 * folio.Font.Report      what the text engine is and what it has cost
 * folio.Font.ClearCache  discards generated fields on disk
 * folio.Font.Shaping     turns OpenType shaping on or off
 */
namespace
{
	Folio::FFolioFontEngine* GetEngine()
	{
		if (!FFolioModule::IsAvailable())
		{
			UE_LOG(LogFolio, Warning, TEXT("the Folio module is not loaded"));
			return nullptr;
		}

		Folio::FFolioFontEngine* Engine = FFolioModule::Get().GetFontEngine();
		if (!Engine)
		{
			UE_LOG(LogFolio, Warning, TEXT("no font engine; RmlUi failed to start"));
		}

		return Engine;
	}

	void Report()
	{
		if (const Folio::FFolioFontEngine* Engine = GetEngine())
		{
			TArray<FString> Lines;
			FString(Engine->GetReport().c_str()).ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				UE_LOG(LogFolio, Display, TEXT("%s"), *Line);
			}
		}
	}

	/**
	 * Takes effect on the next run. Faces already loaded keep the fields they hold in memory, and
	 * would write them straight back.
	 */
	void ClearCache()
	{
		if (!FFolioModule::IsAvailable())
		{
			return;
		}

		const FString& Directory = FFolioModule::Get().GetFontCacheDir();
		if (Directory.IsEmpty())
		{
			return;
		}

		if (IFileManager::Get().DeleteDirectory(*Directory, false, true))
		{
			UE_LOG(LogFolio, Display, TEXT("cleared %s; fields regenerate on the next run"), *Directory);
		}
		else
		{
			UE_LOG(LogFolio, Warning, TEXT("could not clear %s"), *Directory);
		}
	}

	/**
	 * Every open document has to be told to lay out again: the widths change, so the line breaks
	 * do, and nothing else would notice.
	 */
	void SetShaping(const TArray<FString>& Args)
	{
		Folio::FFolioFontEngine* Engine = GetEngine();
		if (!Engine)
		{
			return;
		}

		if (Args.Num() < 1)
		{
			UE_LOG(LogFolio, Display, TEXT("folio.Font.Shaping <0|1>; shaping is %s in this build"),
				Folio::FFolioFontEngine::IsComplexShapingAvailable() ? TEXT("available") : TEXT("unavailable"));
			return;
		}

		const bool bEnabled = Args[0].ToBool() || Args[0] == TEXT("1");
		Engine->SetComplexShaping(bEnabled);

		// A copy of each live set: reloading a document can destroy the thing being iterated.
		for (SFolioCanvas* Canvas : TArray<SFolioCanvas*>(SFolioCanvas::GetLiveCanvases()))
		{
			Canvas->ReloadDocument();
		}
		for (UFolioWorldDocument* Surface : TArray<UFolioWorldDocument*>(UFolioWorldDocument::GetLive()))
		{
			Surface->ReloadDocument();
		}

		UE_LOG(LogFolio, Display, TEXT("shaping %s"), bEnabled ? TEXT("on") : TEXT("off"));
	}

	FAutoConsoleCommand ShapingCommand(
		TEXT("folio.Font.Shaping"),
		TEXT("Turns OpenType shaping on or off and lays every open document out again."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetShaping));

	FAutoConsoleCommand ReportCommand(
		TEXT("folio.Font.Report"),
		TEXT("Prints the text engine, its faces and variation instances, and cache hits and misses."),
		FConsoleCommandDelegate::CreateStatic(&Report));

	FAutoConsoleCommand ClearCacheCommand(
		TEXT("folio.Font.ClearCache"),
		TEXT("Deletes the generated glyph fields under Saved. They are rebuilt on the next run."),
		FConsoleCommandDelegate::CreateStatic(&ClearCache));
}

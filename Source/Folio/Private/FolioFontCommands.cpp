#include "FolioCore/FolioFontEngine.h"
#include "FolioModule.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

/**
 * folio.Font.Report      what the text engine is and what it has cost
 * folio.Font.ClearCache  discards generated fields on disk
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

	FAutoConsoleCommand ReportCommand(
		TEXT("folio.Font.Report"),
		TEXT("Prints the text engine, its faces and variation instances, and cache hits and misses."),
		FConsoleCommandDelegate::CreateStatic(&Report));

	FAutoConsoleCommand ClearCacheCommand(
		TEXT("folio.Font.ClearCache"),
		TEXT("Deletes the generated glyph fields under Saved. They are rebuilt on the next run."),
		FConsoleCommandDelegate::CreateStatic(&ClearCache));
}

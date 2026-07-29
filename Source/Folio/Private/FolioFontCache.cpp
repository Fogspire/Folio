#include "FolioFontCache.h"

#include "FolioModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FFolioFontCache::FFolioFontCache(const FString& InDirectory)
	: Directory(InDirectory)
{
}

FString FFolioFontCache::PathFor(const std::string& Key) const
{
	return Directory / FString(ANSI_TO_TCHAR(Key.c_str())) + TEXT(".folioatlas");
}

bool FFolioFontCache::Read(const std::string& Key, std::vector<unsigned char>& OutData)
{
	if (Directory.IsEmpty())
	{
		return false;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *PathFor(Key)) || Bytes.Num() == 0)
	{
		return false;
	}

	OutData.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
	return true;
}

void FFolioFontCache::Write(const std::string& Key, const std::vector<unsigned char>& Data)
{
	if (Directory.IsEmpty() || Data.empty())
	{
		return;
	}

	const FString FinalPath = PathFor(Key);
	const FString TempPath = FinalPath + TEXT(".tmp");

	// Written beside the target and moved into place. A run that dies midway would otherwise
	// leave a truncated blob that the next run reads back as real fields.
	const TArrayView<const uint8> View(Data.data(), static_cast<int32>(Data.size()));
	if (!FFileHelper::SaveArrayToFile(View, *TempPath))
	{
		UE_LOG(LogFolio, Warning, TEXT("could not write the glyph cache to %s"), *TempPath);
		return;
	}

	IFileManager& Files = IFileManager::Get();
	Files.Delete(*FinalPath, false, true, true);
	if (!Files.Move(*FinalPath, *TempPath, true, true))
	{
		Files.Delete(*TempPath, false, true, true);
	}
}

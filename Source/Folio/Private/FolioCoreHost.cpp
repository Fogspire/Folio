#include "FolioCoreHost.h"

#include "FolioFileInterface.h"
#include "FolioModule.h"
#include "FolioRmlUi.h"

FFolioCoreHost::FFolioCoreHost(FFolioFileInterface& InFiles)
	: Files(InFiles)
{
}

bool FFolioCoreHost::ReadFile(const std::string& Path, std::vector<unsigned char>& OutData)
{
	// Through the file interface rather than the filesystem, so /folio/ and root-relative paths
	// resolve the same way they do for documents, and so this reads from inside a pak.
	const Rml::FileHandle Handle = Files.Open(Path);
	if (!Handle)
	{
		return false;
	}

	Files.Seek(Handle, 0, SEEK_END);
	const size_t Length = Files.Tell(Handle);
	Files.Seek(Handle, 0, SEEK_SET);

	OutData.resize(Length);
	const bool bRead = Length > 0 && Files.Read(OutData.data(), Length, Handle) == Length;
	Files.Close(Handle);

	if (!bRead)
	{
		OutData.clear();
	}
	return bRead;
}

void FFolioCoreHost::LogError(const std::string& Message)
{
	UE_LOG(LogFolio, Error, TEXT("%s"), *Folio::ToFString(Message));
}

void FFolioCoreHost::LogWarning(const std::string& Message)
{
	UE_LOG(LogFolio, Warning, TEXT("%s"), *Folio::ToFString(Message));
}

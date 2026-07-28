#include "FolioFileInterface.h"

#include "FolioModule.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	/** RmlUi hands out an opaque handle; ours is the archive pointer. */
	FArchive* ToArchive(const Rml::FileHandle Handle)
	{
		return reinterpret_cast<FArchive*>(Handle);
	}
}

FFolioFileInterface::FFolioFileInterface(FString InContentRoot, FString InPluginRoot)
	: ContentRoot(MoveTemp(InContentRoot))
	, PluginRoot(MoveTemp(InPluginRoot))
{
	FPaths::NormalizeDirectoryName(ContentRoot);
	FPaths::NormalizeDirectoryName(PluginRoot);
}

FString FFolioFileInterface::Resolve(const Rml::String& Path) const
{
	FString Relative = Folio::ToFString(Path);
	FPaths::NormalizeFilename(Relative);

	// /folio/ is the plugin's own mount, so folio.rcss is reachable from any content root.
	// It has to be checked before the content root, or a game with a ui/folio directory
	// would shadow the base stylesheet.
	const TCHAR* FolioPrefix = TEXT("/folio/");
	if (Relative.StartsWith(FolioPrefix))
	{
		return FPaths::Combine(PluginRoot, TEXT("rcss"), Relative.RightChop(FCString::Strlen(FolioPrefix)));
	}

	if (Relative.StartsWith(TEXT("/")))
	{
		return FPaths::Combine(ContentRoot, Relative.RightChop(1));
	}

	// RmlUi has already joined a relative reference against its document's directory, so an
	// absolute path at this point is a real one.
	if (FPaths::IsRelative(Relative))
	{
		return FPaths::Combine(ContentRoot, Relative);
	}

	return Relative;
}

Rml::FileHandle FFolioFileInterface::Open(const Rml::String& Path)
{
	const FString Absolute = Resolve(Path);

	FArchive* Archive = IFileManager::Get().CreateFileReader(*Absolute);
	if (!Archive)
	{
		UE_LOG(LogFolio, Verbose, TEXT("could not open '%s'"), *Absolute);
		return 0;
	}

	return reinterpret_cast<Rml::FileHandle>(Archive);
}

void FFolioFileInterface::Close(const Rml::FileHandle File)
{
	if (FArchive* Archive = ToArchive(File))
	{
		Archive->Close();
		delete Archive;
	}
}

size_t FFolioFileInterface::Read(void* Buffer, const size_t Size, const Rml::FileHandle File)
{
	FArchive* Archive = ToArchive(File);
	if (!Archive)
	{
		return 0;
	}

	// FArchive has no short read: asking beyond the end is an error rather than a partial
	// read, so the clamp is what turns it into read-what-is-left semantics.
	const int64 Remaining = Archive->TotalSize() - Archive->Tell();
	const int64 ToRead = FMath::Min(static_cast<int64>(Size), FMath::Max(Remaining, int64(0)));

	Archive->Serialize(Buffer, ToRead);
	return static_cast<size_t>(ToRead);
}

bool FFolioFileInterface::Seek(const Rml::FileHandle File, const long Offset, const int Origin)
{
	FArchive* Archive = ToArchive(File);
	if (!Archive)
	{
		return false;
	}

	int64 Target = 0;
	switch (Origin)
	{
	case SEEK_SET: Target = Offset; break;
	case SEEK_CUR: Target = Archive->Tell() + Offset; break;
	case SEEK_END: Target = Archive->TotalSize() + Offset; break;
	default: return false;
	}

	if (Target < 0 || Target > Archive->TotalSize())
	{
		return false;
	}

	Archive->Seek(Target);
	return true;
}

size_t FFolioFileInterface::Tell(const Rml::FileHandle File)
{
	FArchive* Archive = ToArchive(File);
	return Archive ? static_cast<size_t>(Archive->Tell()) : 0;
}

size_t FFolioFileInterface::Length(const Rml::FileHandle File)
{
	FArchive* Archive = ToArchive(File);
	return Archive ? static_cast<size_t>(Archive->TotalSize()) : 0;
}

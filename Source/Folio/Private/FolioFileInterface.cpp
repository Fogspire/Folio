#include "FolioFileInterface.h"

#include "FolioModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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
	const FString Absolute = ResolveInPackage(Path);

#if !UE_BUILD_SHIPPING
	// The override directory mirrors the project, which is also the staged layout, so
	// UI/screens/x.rml overrides as <override>/UI/screens/x.rml and the plugin's own stylesheet as
	// <override>/Plugins/Folio/rcss/folio.rcss. A file that is not there falls through to the pak,
	// so an override directory holding one document is enough.
	if (!OverrideRoot.IsEmpty())
	{
		FString Relative = Absolute;
		if (FPaths::MakePathRelativeTo(Relative, *(ContentRoot / TEXT(""))) && !Relative.StartsWith(TEXT("..")))
		{
			const FString Candidate = FPaths::Combine(OverrideRoot, Relative);
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
	}
#endif

	return Absolute;
}

FString FFolioFileInterface::ResolveInPackage(const Rml::String& Path) const
{
	FString Relative = Folio::ToFString(Path);
	FPaths::NormalizeFilename(Relative);

	// Examples live outside rcss/ because rcss/ is what a packaged build stages, and a demo document
	// is not something to ship. The mount hides that: /folio/examples/ reads the same either way.
	for (const TCHAR* Prefix : { TEXT("/folio/examples/"), TEXT("folio/examples/") })
	{
		if (Relative.StartsWith(Prefix))
		{
			return FPaths::Combine(PluginRoot, TEXT("examples"), Relative.RightChop(FCString::Strlen(Prefix)));
		}
	}

	// /folio/ is the plugin's own mount, so folio.rcss is reachable from any content root.
	// It has to be checked before the content root, or a game with a UI/folio directory
	// would shadow the base stylesheet.
	//
	// The leading slash is optional because it does not survive the trip: RmlUi joins a
	// stylesheet reference against the document that made it, and joining a root-absolute
	// path against a relative document path yields a relative one. So "/folio/folio.rcss"
	// referenced from "UI/examples/panel.rml" arrives here as "folio/folio.rcss". Matching
	// only the slashed form meant the base stylesheet silently never loaded in engine, and
	// every div and p stayed inline as though there were no user-agent stylesheet at all -
	// which is exactly what RmlUi does without one.
	for (const TCHAR* Prefix : { TEXT("/folio/"), TEXT("folio/") })
	{
		if (Relative.StartsWith(Prefix))
		{
			return FPaths::Combine(PluginRoot, TEXT("rcss"), Relative.RightChop(FCString::Strlen(Prefix)));
		}
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

#if !UE_BUILD_SHIPPING
void FFolioFileInterface::SetOverrideRoot(FString Directory)
{
	OverrideRoot = MoveTemp(Directory);
	FPaths::NormalizeDirectoryName(OverrideRoot);
}

FString FFolioFileInterface::OverrideRootFromCommandLine()
{
	const TCHAR* CommandLine = FCommandLine::Get();

	FString Directory;
	if (FParse::Value(CommandLine, TEXT("uidev="), Directory) && !Directory.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(Directory);
	}

	// Bare -uidev means a uidev directory in the project, which in a staged build sits beside the ui
	// tree it shadows. Deliberately not BaseDir: that is Binaries/<Platform> once staged, which is
	// nowhere anyone would think to drop files.
	if (FParse::Param(CommandLine, TEXT("uidev")))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("uidev"));
	}

	return FString();
}
#endif

FString FFolioFileInterface::NormalisePath(const FString& Path)
{
	FString Out = FPaths::ConvertRelativePathToFull(Path);
	FPaths::NormalizeFilename(Out);
	return Out.ToLower();
}

Rml::FileHandle FFolioFileInterface::Open(const Rml::String& Path)
{
	const FString Absolute = Resolve(Path);

	FArchive* Archive = IFileManager::Get().CreateFileReader(*Absolute);

#if WITH_EDITOR
	// Editors save by writing a temporary file and renaming over the target, so for a few
	// milliseconds after a change notification the file is locked or zero length. Without this,
	// every other save during hot reload looks like a parse error. Only a file that is already
	// there is waited for, so a genuinely missing one still fails immediately.
	if ((!Archive || Archive->TotalSize() == 0) && FPaths::FileExists(Absolute))
	{
		const double Deadline = FPlatformTime::Seconds() + 0.25;
		while ((!Archive || Archive->TotalSize() == 0) && FPlatformTime::Seconds() < Deadline)
		{
			delete Archive;
			FPlatformProcess::Sleep(0.01f);
			Archive = IFileManager::Get().CreateFileReader(*Absolute);
		}
	}
#endif

	if (!Archive)
	{
		UE_LOG(LogFolio, Verbose, TEXT("could not open '%s'"), *Absolute);
		return 0;
	}

	Opened.Add(NormalisePath(Absolute));

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

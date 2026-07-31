#include "FolioDocumentPath.h"

#include "FolioFileInterface.h"
#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	/** Every `.rml` under Directory, as paths relative to it, forward slashed. */
	void FindDocuments(const FString& Directory, const FString& Prefix, TArray<FString>& Out)
	{
		if (Directory.IsEmpty() || !IFileManager::Get().DirectoryExists(*Directory))
		{
			return;
		}

		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *Directory, TEXT("*.rml"), true, false);

		for (FString& File : Found)
		{
			FPaths::MakePathRelativeTo(File, *(Directory / TEXT("")));
			FPaths::NormalizeFilename(File);
			Out.Add(Prefix + File);
		}
	}
}

TArray<FString> UFolioDocumentPathLibrary::GetDocumentOptions()
{
	TArray<FString> Options;

	if (!FFolioModule::IsAvailable())
	{
		return Options;
	}

	const FFolioModule& Module = FFolioModule::Get();

	// The game's own documents first and unprefixed, because those are what anyone is looking for.
	FindDocuments(Module.GetContentRoot(), FString(), Options);

	// Then the plugin's, under the mounts that make them reachable from any content root. A recipe is
	// meant to be copied rather than pointed at, so only the two that are not - the base stylesheet's
	// directory holds no documents, and examples are things to look at - appear here.
	FindDocuments(Module.GetPluginRoot() / TEXT("examples"), TEXT("/folio/examples/"), Options);
	FindDocuments(Module.GetPluginRoot() / TEXT("rcss"), TEXT("/folio/"), Options);

	Options.Sort();
	return Options;
}

FString UFolioDocumentPathLibrary::ResolveDocumentPath(const FString& DocumentPath)
{
	if (DocumentPath.IsEmpty() || !FFolioModule::IsAvailable())
	{
		return FString();
	}

	const FFolioFileInterface* Files = FFolioModule::Get().GetFileInterface();
	if (!Files)
	{
		return FString();
	}

	return Files->Resolve(Folio::ToRmlString(DocumentPath));
}

bool UFolioDocumentPathLibrary::DocumentExists(const FString& DocumentPath)
{
	const FString Resolved = ResolveDocumentPath(DocumentPath);
	return !Resolved.IsEmpty() && FPaths::FileExists(Resolved);
}

FString UFolioDocumentPathLibrary::MakeDocumentPathPortable(const FString& DocumentPath)
{
	if (DocumentPath.IsEmpty() || !FFolioModule::IsAvailable())
	{
		return DocumentPath;
	}

	FString Path = DocumentPath;
	FPaths::NormalizeFilename(Path);

	if (FPaths::IsRelative(Path))
	{
		return Path;
	}

	const FFolioModule& Module = FFolioModule::Get();

	// The plugin first: its directory can sit inside the project, so testing the content root first
	// would turn a plugin document into Plugins/Folio/examples/... which resolves only from this
	// project rather than from any.
	struct FMount { FString Root; const TCHAR* Prefix; };
	const FMount Mounts[] = {
		{ Module.GetPluginRoot() / TEXT("examples"), TEXT("/folio/examples/") },
		{ Module.GetPluginRoot() / TEXT("rcss"),     TEXT("/folio/") },
		{ Module.GetContentRoot(),                   TEXT("") },
	};

	for (const FMount& Mount : Mounts)
	{
		FString Relative = Path;
		if (FPaths::MakePathRelativeTo(Relative, *(Mount.Root / TEXT(""))) && !Relative.StartsWith(TEXT("..")))
		{
			return Mount.Prefix + Relative;
		}
	}

	return Path;
}

void UFolioDocumentPathLibrary::ReportMissingDocument(const FString& DocumentPath, const TCHAR* Context)
{
	const FString Resolved = ResolveDocumentPath(DocumentPath);

	UE_LOG(LogFolio, Error,
		TEXT("%s: '%s' does not exist. It resolved to '%s'."),
		Context, *DocumentPath, Resolved.IsEmpty() ? TEXT("nothing") : *Resolved);

	if (FPaths::IsRelative(DocumentPath))
	{
		UE_LOG(LogFolio, Error,
			TEXT("%s: a relative path is read against the content root, which is '%s'."),
			Context, *(FFolioModule::IsAvailable() ? FFolioModule::Get().GetContentRoot() : FString()));
	}
	else
	{
		UE_LOG(LogFolio, Error,
			TEXT("%s: that is an absolute path. It will not resolve on anyone else's machine either - ")
			TEXT("use a project-relative one such as ui/screens/hud.rml."),
			Context);
	}

	const TArray<FString> Options = GetDocumentOptions();
	if (Options.IsEmpty())
	{
		UE_LOG(LogFolio, Error, TEXT("%s: no .rml files were found at all."), Context);
		return;
	}

	// Listed rather than counted: the answer is almost always a typo against something in this list,
	// and printing it turns a guess into a comparison.
	UE_LOG(LogFolio, Error, TEXT("%s: documents that do exist:"), Context);
	for (const FString& Option : Options)
	{
		UE_LOG(LogFolio, Error, TEXT("    %s"), *Option);
	}
}

#include "FolioDocumentPath.h"

#include "FolioFileInterface.h"
#include "FolioModule.h"
#include "FolioSettings.h"
#include "FolioRmlUi.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "UObject/PropertyTag.h"
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

bool FFolioDocumentPath::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	if (Tag.Type != NAME_StrProperty)
	{
		return false;
	}

	Slot << Path;
	return true;
}

TArray<FString> UFolioDocumentPathLibrary::GetDocumentOptions()
{
	TArray<FString> Options;

	if (!FFolioModule::IsAvailable())
	{
		return Options;
	}

	const FFolioModule& Module = FFolioModule::Get();

	const UFolioSettings* Settings = GetDefault<UFolioSettings>();

	// The game's own documents first and unprefixed, because those are what anyone is looking for.
	FindDocuments(Module.GetContentRoot(), FString(), Options);

	// Then the plugin's, under the mounts that make them reachable from any content root. A recipe is
	// meant to be copied rather than pointed at, so only the two that are not - the base stylesheet's
	// directory holds no documents, and examples are things to look at - appear here.
	if (!Settings || Settings->bShowPluginDocumentsInPicker)
	{
		FindDocuments(Module.GetPluginRoot() / TEXT("examples"), TEXT("/folio/examples/"), Options);
		FindDocuments(Module.GetPluginRoot() / TEXT("rcss"), TEXT("/folio/"), Options);
	}

	if (Settings)
	{
		for (const FString& Hidden : Settings->HiddenDocumentDirectories)
		{
			FString Prefix = Hidden;
			FPaths::NormalizeFilename(Prefix);
			if (Prefix.IsEmpty())
			{
				continue;
			}

			// A prefix rather than an exact path, so one entry hides a tree. The trailing slash stops
			// "ui/frag" from also hiding "ui/fragments_used".
			if (!Prefix.EndsWith(TEXT("/")))
			{
				Prefix += TEXT("/");
			}

			Options.RemoveAll([&Prefix](const FString& Option) { return Option.StartsWith(Prefix); });
		}
	}

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

bool UFolioDocumentPathLibrary::GetDeclaredDocumentSize(const FString& DocumentPath, FVector2D& OutSize)
{
	const FString Resolved = ResolveDocumentPath(DocumentPath);

	FString Markup;
	if (Resolved.IsEmpty() || !FFileHelper::LoadFileToString(Markup, *Resolved))
	{
		return false;
	}

	// The head only, so the same text inside a template's body cannot be mistaken for a declaration.
	const int32 HeadEnd = Markup.Find(TEXT("</head>"), ESearchCase::IgnoreCase);
	if (HeadEnd != INDEX_NONE)
	{
		Markup.LeftInline(HeadEnd);
	}

	const int32 Anchor = Markup.Find(TEXT("folio-size"), ESearchCase::IgnoreCase);
	if (Anchor == INDEX_NONE)
	{
		return false;
	}

	const int32 ContentAt = Markup.Find(TEXT("content"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Anchor);
	if (ContentAt == INDEX_NONE)
	{
		return false;
	}

	// Between the quotes after `content=`, taking whichever quote character opened it so both forms
	// work without deciding which one the author should have used.
	int32 Open = INDEX_NONE;
	for (int32 Index = ContentAt; Index < Markup.Len(); ++Index)
	{
		if (Markup[Index] == TEXT('"') || Markup[Index] == TEXT('\''))
		{
			Open = Index;
			break;
		}
	}

	if (Open == INDEX_NONE)
	{
		return false;
	}

	const int32 Close = Markup.Find(FString::Chr(Markup[Open]), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, Open + 1);

	if (Close == INDEX_NONE || Close <= Open + 1)
	{
		return false;
	}

	FString Value = Markup.Mid(Open + 1, Close - Open - 1);
	Value.TrimStartAndEndInline();

	FString Width;
	FString Height;
	if (!Value.Split(TEXT("x"), &Width, &Height, ESearchCase::IgnoreCase))
	{
		return false;
	}

	const double W = FCString::Atod(*Width.TrimStartAndEnd());
	const double H = FCString::Atod(*Height.TrimStartAndEnd());

	if (W <= 0.0 || H <= 0.0)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' declares folio-size '%s', which is not two positive numbers. Ignoring it."),
			*DocumentPath, *Value);
		return false;
	}

	OutSize = FVector2D(W, H);
	return true;
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

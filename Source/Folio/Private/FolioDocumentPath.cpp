#include "FolioDocumentPath.h"

#include "FolioFileInterface.h"
#include "FolioModule.h"
#include "FolioSettings.h"
#include "FolioRmlUi.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "UObject/PropertyTag.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Components/ActorComponent.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Logging/MessageLog.h"
#include "Misc/DataValidation.h"
#include "Misc/UObjectToken.h"
#include "Settings/ProjectPackagingSettings.h"
#endif

namespace
{
	/**
	 * Whether a directory of this name is output rather than content.
	 *
	 * Pruned during the walk rather than filtered after it, because the walk is the cost. A project's
	 * Intermediate and DerivedDataCache hold tens of thousands of files between them, and the vendored
	 * RmlUi source under the plugin's own build directory holds several hundred `.rml` samples - which
	 * are documents, so no filter on the results could tell them from the game's.
	 */
	bool IsOutputDirectory(const FString& Name)
	{
		static const TCHAR* const Ignored[] = {
			TEXT("Binaries"), TEXT("Build"), TEXT("DerivedDataCache"), TEXT("Intermediate"),
			TEXT("Saved"), TEXT("node_modules"), TEXT("__pycache__"),
			TEXT(".git"), TEXT(".svn"), TEXT(".vs"), TEXT(".vscode"), TEXT(".idea"),
		};

		for (const TCHAR* const Candidate : Ignored)
		{
			if (Name.Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Every `.rml` under Directory, as paths relative to it, forward slashed.
	 *
	 * SkipSubtree is one relative directory to leave out, for the plugin's own tree: its documents are
	 * already offered under the `/folio/` mounts, and the rest of it is test fixtures and recipes.
	 */
	void FindDocuments(const FString& Directory, const FString& Prefix, TArray<FString>& Out,
		const FString& SkipSubtree = FString())
	{
		if (Directory.IsEmpty() || !IFileManager::Get().DirectoryExists(*Directory))
		{
			return;
		}

		// Breadth first over directories we have decided to enter, rather than IterateDirectoryRecursively,
		// which offers no way to decline one.
		TArray<FString> Pending;
		Pending.Add(FString());

		while (!Pending.IsEmpty())
		{
			const FString Relative = Pending.Pop();
			const FString Absolute = Relative.IsEmpty() ? Directory : Directory / Relative;

			IFileManager::Get().IterateDirectory(*Absolute,
				[&Pending, &Out, &Relative, &Prefix, &SkipSubtree](const TCHAR* Path,
					const bool bIsDirectory) -> bool
				{
					const FString Name = FPaths::GetCleanFilename(Path);
					const FString Child = Relative.IsEmpty() ? Name : Relative / Name;

					if (bIsDirectory)
					{
						if (!IsOutputDirectory(Name)
							&& (SkipSubtree.IsEmpty() || !Child.Equals(SkipSubtree, ESearchCase::IgnoreCase)))
						{
							Pending.Add(Child);
						}
					}
					else if (Name.EndsWith(TEXT(".rml"), ESearchCase::IgnoreCase))
					{
						Out.Add(Prefix + Child);
					}

					return true;
				});
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
	// Folio's own tree comes out: it sits under the project, so the walk would otherwise reach it, and
	// everything in it worth offering is already below under a mount.
	FString PluginRelative = Module.GetPluginRoot();
	if (!PluginRelative.IsEmpty() && FPaths::MakePathRelativeTo(PluginRelative, *(Module.GetContentRoot() / TEXT(""))))
	{
		PluginRelative.RemoveFromEnd(TEXT("/"));
	}
	else
	{
		PluginRelative.Empty();
	}

	FindDocuments(Module.GetContentRoot(), FString(), Options, PluginRelative);

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
			// "UI/frag" from also hiding "UI/fragments_used".
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

void UFolioDocumentPathLibrary::ReportMissingDocument(const FString& DocumentPath, const TCHAR* Context,
	const UObject* Referencer)
{
	const FString Resolved = ResolveDocumentPath(DocumentPath);

	UE_LOG(LogFolio, Error,
		TEXT("%s: '%s' does not exist. It resolved to '%s'."),
		Context, *DocumentPath, Resolved.IsEmpty() ? TEXT("nothing") : *Resolved);

	// The one thing the report was missing. A path is set on an object somewhere in a level or a
	// Blueprint, and without naming it the only way to find it is to open things until one matches.
	if (Referencer)
	{
		const AActor* Actor = Cast<AActor>(Referencer);
		if (const UActorComponent* Component = Cast<UActorComponent>(Referencer))
		{
			Actor = Component->GetOwner();
		}

		// The asset named separately from the object path. During a cook there is no level open to go
		// looking in, so the first thing anyone needs is which file to fix.
		const UPackage* Package = Referencer->GetPackage();

		UE_LOG(LogFolio, Error, TEXT("%s: set in asset '%s', on '%s'%s."), Context,
			Package ? *Package->GetName() : TEXT("unknown"), *Referencer->GetPathName(),
			Actor ? *FString::Printf(TEXT(", actor '%s'"), *Actor->GetActorNameOrLabel()) : TEXT(""));

#if WITH_EDITOR
		// The same thing again as a message anyone can click. A log line names the asset; a token opens
		// it, which is the difference between reading a path and fixing the mistake.
		if (GIsEditor)
		{
			FMessageLog Log(FolioMessageLogName);

			const TSharedRef<FTokenizedMessage> Message = Log.Error()
				->AddToken(FTextToken::Create(FText::Format(
					NSLOCTEXT("Folio", "MissingDocumentToken", "'{0}' does not exist, named by"),
					FText::FromString(DocumentPath))))
				->AddToken(FUObjectToken::Create(Referencer));

			if (Package)
			{
				Message->AddToken(FTextToken::Create(NSLOCTEXT("Folio", "MissingDocumentIn", "in")))
					->AddToken(FAssetNameToken::Create(Package->GetName()));
			}

			// Not while cooking: there is no window to raise, and a commandlet that stops to open one
			// is a build that hangs.
			if (!IsRunningCommandlet())
			{
				Log.Open(EMessageSeverity::Error, /*bForce*/ false);
			}
		}
#endif
	}

	if (DocumentPath.StartsWith(TEXT("/folio/")))
	{
		UE_LOG(LogFolio, Error,
			TEXT("%s: /folio/ is the plugin's own mount, so the file is looked for under '%s'."),
			Context, *(FFolioModule::IsAvailable() ? FFolioModule::Get().GetPluginRoot() : FString()));
	}
	else if (FPaths::IsRelative(DocumentPath))
	{
		UE_LOG(LogFolio, Error,
			TEXT("%s: a relative path is read against the content root, which is '%s'."),
			Context, *(FFolioModule::IsAvailable() ? FFolioModule::Get().GetContentRoot() : FString()));
	}
	else
	{
		UE_LOG(LogFolio, Error,
			TEXT("%s: that is an absolute path. It will not resolve on anyone else's machine either - ")
			TEXT("use a project-relative one such as UI/screens/hud.rml."),
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

#if WITH_EDITOR

namespace
{
	/**
	 * Whether a packaged build will contain this file.
	 *
	 * DirectoriesToAlwaysStageAsUFS is what puts a loose file in a package, and its entries are
	 * relative to Content rather than to the project, so `../UI` means the project's UI tree. A
	 * document outside every one of them loads in the editor and in a standalone run, which read the
	 * disk, and is simply absent once packaged.
	 */
	bool IsStaged(const FString& AbsoluteFile)
	{
		const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
		if (!Packaging)
		{
			return true;
		}

		FString File = AbsoluteFile;
		FPaths::NormalizeFilename(File);

		for (const FDirectoryPath& Staged : Packaging->DirectoriesToAlwaysStageAsUFS)
		{
			if (Staged.Path.IsEmpty())
			{
				continue;
			}

			FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Staged.Path);
			FPaths::NormalizeDirectoryName(Root);
			Root += TEXT("/");

			if (File.StartsWith(Root, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

EFolioDocumentPathIssue UFolioDocumentPathLibrary::DescribeDocumentPathIssue(
	const UActorComponent& Component, const FString& DocumentPath, FText& OutMessage)
{
	const AActor* Owner = Component.GetOwner();

	// A component on a Blueprint may be given its document from code before it registers, so an empty
	// path is only wrong once something has been placed in a level.
	const bool bPlaced = Owner && Owner->GetLevel() != nullptr;

	// The component named with its actor, because a level holds many of both and the component's own
	// name is usually the class name repeated.
	const FText Where = Owner
		? FText::Format(NSLOCTEXT("Folio", "ComponentOnActor", "{0} on '{1}'"),
			FText::FromString(Component.GetName()), FText::FromString(Owner->GetActorNameOrLabel()))
		: FText::FromString(Component.GetName());

	FText& Message = OutMessage;

	if (DocumentPath.IsEmpty())
	{
		if (!bPlaced)
		{
			return EFolioDocumentPathIssue::None;
		}

		Message = FText::Format(
			NSLOCTEXT("Folio", "NoDocument", "{0} has no document assigned, so it will render nothing."),
			Where);
	}
	else if (!DocumentExists(DocumentPath))
	{
		// Reported whether placed or not: a path naming nothing is wrong wherever it was written, and
		// an absolute one is wrong on every machine but this.
		const FString Resolved = ResolveDocumentPath(DocumentPath);

		Message = FText::Format(
			NSLOCTEXT("Folio", "MissingDocument",
				"{0} names '{1}', which does not exist. It resolves to '{2}'."),
			Where,
			FText::FromString(DocumentPath),
			FText::FromString(Resolved.IsEmpty() ? TEXT("nothing") : *Resolved));
	}
	else if (!FPaths::IsRelative(DocumentPath) && !DocumentPath.StartsWith(TEXT("/folio/")))
	{
		// The mount is portable, and it is the one path that begins with a slash and is not a
		// path on this machine.
		Message = FText::Format(
			NSLOCTEXT("Folio", "AbsoluteDocument",
				"{0} names '{1}', an absolute path. It will not resolve on another machine - use a "
				"project-relative path such as UI/screens/hud.rml."),
			Where,
			FText::FromString(DocumentPath));
	}
	else if (!IsStaged(ResolveDocumentPath(DocumentPath)))
	{
		Message = FText::Format(
			NSLOCTEXT("Folio", "UnstagedDocument",
				"{0} names '{1}', which is not in a directory packaging stages. It loads in the editor "
				"and in a standalone run and will be missing from a packaged build. Add its directory "
				"to Project Settings > Packaging > Additional Non-Asset Directories to Package."),
			Where,
			FText::FromString(DocumentPath));

		return EFolioDocumentPathIssue::Unstaged;
	}
	else
	{
		return EFolioDocumentPathIssue::None;
	}

	return DocumentPath.IsEmpty()
		? EFolioDocumentPathIssue::Unassigned
		: EFolioDocumentPathIssue::Unresolved;
}

bool UFolioDocumentPathLibrary::ValidateDocumentPath(const UActorComponent& Component,
	const FString& DocumentPath, FDataValidationContext* Validation)
{
	FText Message;
	const EFolioDocumentPathIssue Issue = DescribeDocumentPathIssue(Component, DocumentPath, Message);
	if (Issue == EFolioDocumentPathIssue::None)
	{
		return false;
	}

	// A document that will not be staged still works everywhere it is being looked at today, so it is
	// a warning. Everything else renders nothing right now.
	const EMessageSeverity::Type Severity = Issue == EFolioDocumentPathIssue::Unstaged
		? EMessageSeverity::Warning
		: EMessageSeverity::Error;

	AActor* Owner = const_cast<AActor*>(Component.GetOwner());

	// Tokenised either way, so an entry is a thing to click rather than a name to go hunting for.
	// Validation runs on save and on a cook, Map Check when someone asks; the reader is the same
	// person wanting the same actor.
	if (Validation)
	{
		const TSharedRef<FTokenizedMessage> Entry = Validation->AddMessage(Severity, FText::GetEmpty());

		if (Owner)
		{
			Entry->AddToken(FUObjectToken::Create(Owner));
		}
		Entry->AddToken(FTextToken::Create(Message));
	}
	else if (Owner)
	{
		FMessageLog(TEXT("MapCheck")).Message(Severity)
			->AddToken(FUObjectToken::Create(Owner))
			->AddToken(FTextToken::Create(Message));
	}

	return true;
}

#endif

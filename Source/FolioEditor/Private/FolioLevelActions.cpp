#include "FolioLevelActions.h"

#include "FolioDocumentPath.h"
#include "FolioMarkerDocument.h"
#include "FolioModule.h"
#include "FolioWorldDocument.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/PlatformFileManager.h"
#include "ISourceControlModule.h"
#include "Misc/FileHelper.h"
#include "SourceControlHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "Folio"

namespace
{
	/** What is wrong with a component's document, and what to say about it. */
	EFolioDocumentPathIssue DescribeIssue(const UActorComponent& Component, FText& OutMessage)
	{
		if (const UFolioWorldDocument* World = Cast<UFolioWorldDocument>(&Component))
		{
			return UFolioDocumentPathLibrary::DescribeDocumentPathIssue(
				*World, World->DocumentPath, OutMessage);
		}

		if (const UFolioMarkerDocument* Marker = Cast<UFolioMarkerDocument>(&Component))
		{
			return UFolioDocumentPathLibrary::DescribeDocumentPathIssue(
				*Marker, Marker->DocumentPath, OutMessage);
		}

		return EFolioDocumentPathIssue::None;
	}

	/**
	 * A toast rather than the Message Log.
	 *
	 * Raising a log window to say nothing is wrong is worse than saying nothing: it takes the screen
	 * away from whatever was being looked at, to deliver an answer that fits in a sentence. The log is
	 * still written either way, so the detail is a click away when it is wanted.
	 */
	void Notify(const FText& Text, const bool bFailed)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = bFailed ? 6.0f : 3.0f;
		Info.bUseSuccessFailIcons = true;

		if (bFailed)
		{
			Info.Hyperlink = FSimpleDelegate::CreateLambda([]
			{
				FMessageLog(FolioMessageLogName).Open(EMessageSeverity::Error, /*bForce*/ true);
			});
			Info.HyperlinkText = LOCTEXT("ShowDocumentIssues", "Show them");
		}

		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bFailed ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}
	}
}

void FolioLevelActions::SelectActorsWithBrokenDocuments()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	TArray<AActor*> Broken;
	int32 Unassigned = 0;
	int32 Unresolved = 0;
	int32 Unstaged = 0;

	FMessageLog Log(FolioMessageLogName);
	Log.NewPage(LOCTEXT("BrokenDocumentsPage", "Documents"));

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		bool bAdded = false;

		for (const UActorComponent* Component : Actor->GetComponents())
		{
			if (!Component)
			{
				continue;
			}

			FText Message;
			const EFolioDocumentPathIssue Issue = DescribeIssue(*Component, Message);
			if (Issue == EFolioDocumentPathIssue::None)
			{
				continue;
			}

			const bool bUnstaged = Issue == EFolioDocumentPathIssue::Unstaged;
			if (bUnstaged) { Unstaged++; }
			else if (Issue == EFolioDocumentPathIssue::Unassigned) { Unassigned++; }
			else { Unresolved++; }

			// Every issue listed, but the actor selected once: one actor can carry several documents
			// and selecting it twice does nothing useful.
			Log.Message(bUnstaged ? EMessageSeverity::Warning : EMessageSeverity::Error)
				->AddToken(FUObjectToken::Create(Actor))
				->AddToken(FTextToken::Create(Message));

			if (!bAdded)
			{
				Broken.Add(Actor);
				bAdded = true;
			}
		}
	}

	if (Broken.IsEmpty())
	{
		Notify(LOCTEXT("NoBrokenDocuments", "Every document path in this level resolves."),
			/*bFailed*/ false);
		return;
	}

	// Transacted, because a selection change is undoable and someone who ran this by accident over a
	// careful selection wants it back.
	const FScopedTransaction Transaction(LOCTEXT("SelectBrokenDocuments", "Select Broken Documents"));

	GEditor->SelectNone(/*bNoteSelectionChange*/ false, /*bDeselectBSPSurfs*/ true);
	for (AActor* Actor : Broken)
	{
		GEditor->SelectActor(Actor, /*bInSelected*/ true, /*bNotify*/ false);
	}
	GEditor->NoteSelectionChange();

	// Both counts, always, including the zero: "3 unassigned" alone reads as the whole answer, and
	// somebody who has just fixed the other kind needs to see that it went.
	Notify(FText::Format(
		LOCTEXT("BrokenDocumentsFound",
			"Selected {0} {0}|plural(one=actor,other=actors): {1} unassigned, {2} naming nothing, "
			"{3} that packaging will not stage."),
		FText::AsNumber(Broken.Num()), FText::AsNumber(Unassigned), FText::AsNumber(Unresolved),
		FText::AsNumber(Unstaged)),
		/*bFailed*/ true);
}


namespace
{
	/** Every document path set on a component in this level, resolved to a file on disk. */
	void GatherDocumentFiles(UWorld& World, TArray<FString>& OutFiles)
	{
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			for (const UActorComponent* Component : (*It)->GetComponents())
			{
				FString Path;
				if (const UFolioWorldDocument* Doc = Cast<UFolioWorldDocument>(Component))
				{
					Path = Doc->DocumentPath.Path;
				}
				else if (const UFolioMarkerDocument* Marker = Cast<UFolioMarkerDocument>(Component))
				{
					Path = Marker->DocumentPath.Path;
				}

				if (Path.IsEmpty())
				{
					continue;
				}

				const FString Resolved = UFolioDocumentPathLibrary::ResolveDocumentPath(Path);
				if (!Resolved.IsEmpty() && FPaths::FileExists(Resolved))
				{
					OutFiles.AddUnique(Resolved);
				}
			}
		}
	}

	/**
	 * The directory that has to be staged for a document to be in a packaged build.
	 *
	 * A tree rather than the file's own directory: a document links stylesheets and names art beside
	 * it, and staging only the directory it happens to sit in leaves those behind.
	 */
	FString StagingRootFor(const FString& AbsoluteFile)
	{
		const FFolioModule& Folio = FFolioModule::Get();

		TArray<FString> Roots;
		Roots.Add(Folio.GetUiRoot());
		if (!Folio.GetPluginRoot().IsEmpty())
		{
			Roots.Add(Folio.GetPluginRoot() / TEXT("examples"));
			Roots.Add(Folio.GetPluginRoot() / TEXT("rcss"));
		}

		for (FString& Root : Roots)
		{
			FPaths::NormalizeDirectoryName(Root);
			if (AbsoluteFile.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase))
			{
				return Root;
			}
		}

		return FPaths::GetPath(AbsoluteFile);
	}

	/** Whether the staging list already covers this directory, or something above it. */
	bool AlreadyStaged(const UProjectPackagingSettings& Packaging, const FString& Directory)
	{
		for (const FDirectoryPath& Staged : Packaging.DirectoriesToAlwaysStageAsUFS)
		{
			FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Staged.Path);
			FPaths::NormalizeDirectoryName(Root);

			if (Directory.StartsWith(Root, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}

		return false;
	}
}

void FolioLevelActions::StageDocumentsUsedInLevel()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UProjectPackagingSettings* Packaging = GetMutableDefault<UProjectPackagingSettings>();
	if (!World || !Packaging || !FFolioModule::IsAvailable())
	{
		return;
	}

	TArray<FString> Files;
	GatherDocumentFiles(*World, Files);

	TArray<FString> Added;
	for (const FString& File : Files)
	{
		const FString Root = StagingRootFor(File);
		if (Root.IsEmpty() || AlreadyStaged(*Packaging, Root))
		{
			continue;
		}

		// Relative to Content, which is what the setting means by a path, and what the leading `../`
		// in every existing entry is doing.
		FString Relative = Root;
		FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectContentDir());

		FDirectoryPath Entry;
		Entry.Path = Relative;
		Packaging->DirectoriesToAlwaysStageAsUFS.Add(Entry);
		Added.AddUnique(Relative);
	}

	if (Added.IsEmpty())
	{
		Notify(FText::Format(
			LOCTEXT("NothingToStage",
				"Nothing to add: every directory this level's {0} {0}|plural(one=document,other=documents) "
				"needs is already staged."),
			FText::AsNumber(Files.Num())), /*bFailed*/ false);
		return;
	}

	Packaging->TryUpdateDefaultConfigFile();

	FMessageLog Log(FolioMessageLogName);
	Log.NewPage(LOCTEXT("StagingPage", "Packaging"));
	for (const FString& Relative : Added)
	{
		Log.Info()->AddToken(FTextToken::Create(FText::Format(
			LOCTEXT("StagedDirectory", "Added (Path=\"{0}\") to the packaging staging list."),
			FText::FromString(Relative))));
	}

	Log.Open(EMessageSeverity::Info, /*bForce*/ true);

	Notify(FText::Format(
		LOCTEXT("StagedDirectories",
			"Staged {0} {0}|plural(one=directory,other=directories) for packaging."),
		FText::AsNumber(Added.Num())), /*bFailed*/ false);
}

void FolioLevelActions::CheckStagedDirectories()
{
	if (!FFolioModule::IsAvailable())
	{
		return;
	}

	const FString UiRoot = FFolioModule::Get().GetUiRoot();

	// Nothing to warn about until there is something to lose. A project that keeps no documents of its
	// own is using Folio's, and those are the per-document warning's business.
	TArray<FString> Documents;
	IFileManager::Get().FindFilesRecursive(Documents, *UiRoot, TEXT("*.rml"), true, false);
	if (Documents.IsEmpty())
	{
		return;
	}

	const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
	if (!Packaging)
	{
		return;
	}

	FString Wanted = UiRoot;
	FPaths::NormalizeDirectoryName(Wanted);

	for (const FDirectoryPath& Staged : Packaging->DirectoriesToAlwaysStageAsUFS)
	{
		FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / Staged.Path);
		FPaths::NormalizeDirectoryName(Root);

		// The tree itself or anything above it, since staging a parent takes the children with it.
		if (Wanted.StartsWith(Root, ESearchCase::IgnoreCase))
		{
			return;
		}
	}

	// The relative form, because that is what has to be typed into the setting: entries are resolved
	// against Content rather than the project root, which is what the leading `../` is doing.
	FString Relative = UiRoot;
	FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectContentDir());

	FMessageLog Log(FolioMessageLogName);
	Log.NewPage(LOCTEXT("StagingPage", "Packaging"));
	Log.Warning()->AddToken(FTextToken::Create(FText::Format(
		LOCTEXT("UiTreeUnstaged",
			"{0} {0}|plural(one=document,other=documents) live in '{1}', which packaging will not stage. "
			"Every one of them will be missing from a packaged build, while the editor and a standalone "
			"run keep working. Add (Path=\"{2}\") to Project Settings > Packaging > Additional "
			"Non-Asset Directories to Package."),
		FText::AsNumber(Documents.Num()), FText::FromString(UiRoot), FText::FromString(Relative))));

	Log.Open(EMessageSeverity::Warning, /*bForce*/ true);
}

namespace
{
	/** One console variable, and the two lines that say why a document needs it off. */
	struct FCommonUiOverride
	{
		const TCHAR* Key;
		const TCHAR* Value;
		const TCHAR* Comment;
	};

	const FCommonUiOverride CommonUiOverrides[] =
	{
		{
			TEXT("CommonUI.ShouldVirtualAcceptSimulateMouseButton"), TEXT("0"),
			TEXT("; Folio: the pad's accept button becomes a left click at CommonUI's software cursor, so a confirm\n"
				"; presses whatever the cursor is over rather than what is focused.")
		},
		{
			TEXT("CommonUI.EnableVirtualPointer"), TEXT("0"),
			TEXT("; Folio: the stick drives that cursor, so a player on a pad is generating real mouse movement.")
		},
		{
			TEXT("CommonUI.SyncCursorToFocusedWidgetOnInputMethodChange"), TEXT("0"),
			TEXT("; Folio: picking up the pad warps the cursor onto the focused widget, which reads as the mouse\n"
				"; coming back and hands the device straight to the pointer again.")
		},
	};

	const TCHAR* ConsoleVariablesSection = TEXT("[ConsoleVariables]");

	/** Whether this line is that key being set, whatever it is set to and however it is spaced. */
	bool LineSets(const FString& Line, const TCHAR* Key)
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (!Trimmed.RemoveFromStart(Key, ESearchCase::IgnoreCase))
		{
			return false;
		}

		return Trimmed.TrimStart().StartsWith(TEXT("="));
	}

	/** Makes the file writable, through source control where there is any. */
	bool MakeWritable(const FString& File)
	{
		if (ISourceControlModule::Get().IsEnabled())
		{
			USourceControlHelpers::CheckOutOrAddFile(File, /*bSilent*/ true);
		}

		IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
		if (!Platform.FileExists(*File) || !Platform.IsReadOnly(*File))
		{
			return true;
		}

		return Platform.SetReadOnly(*File, false);
	}
}

bool FolioLevelActions::HasCommonUI()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI"));
}

void FolioLevelActions::SetCommonUiConsoleVariables()
{
	const FString Ini = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini"));

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Ini))
	{
		Notify(FText::Format(LOCTEXT("NoDefaultEngineIni", "Could not read '{0}'."),
			FText::FromString(Ini)), /*bFailed*/ true);
		return;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);

	// The section as it stands. Everything already in it is left alone, so the only question this
	// answers is where a line that is missing goes.
	int32 SectionStart = INDEX_NONE;
	int32 SectionEnd = Lines.Num();
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FString Trimmed = Lines[Index].TrimStartAndEnd();
		if (SectionStart == INDEX_NONE)
		{
			if (Trimmed.Equals(ConsoleVariablesSection, ESearchCase::IgnoreCase))
			{
				SectionStart = Index + 1;
			}
		}
		else if (Trimmed.StartsWith(TEXT("[")))
		{
			SectionEnd = Index;
			break;
		}
	}

	TArray<FString> Added;
	TArray<FString> Block;
	for (const FCommonUiOverride& Override : CommonUiOverrides)
	{
		bool bPresent = false;
		for (int32 Index = SectionStart; Index != INDEX_NONE && Index < SectionEnd && !bPresent; ++Index)
		{
			bPresent = LineSets(Lines[Index], Override.Key);
		}

		if (bPresent)
		{
			continue;
		}

		TArray<FString> CommentLines;
		FString(Override.Comment).ParseIntoArrayLines(CommentLines, /*bCullEmpty*/ false);

		Block.Add(FString());
		Block.Append(CommentLines);
		Block.Add(FString::Printf(TEXT("%s=%s"), Override.Key, Override.Value));

		Added.Add(Override.Key);
	}

	if (Added.IsEmpty())
	{
		Notify(LOCTEXT("CommonUiAlreadySet",
			"Nothing to add: DefaultEngine.ini already has every CommonUI variable a document needs."),
			/*bFailed*/ false);
		return;
	}

	if (SectionStart == INDEX_NONE)
	{
		Lines.Add(FString());
		Lines.Add(ConsoleVariablesSection);
		SectionEnd = Lines.Num();
	}

	Lines.Insert(Block, SectionEnd);

	// The file's own line ending, so a project keeping these files with Unix endings gets three new
	// lines in its diff rather than every line in the file.
	const TCHAR* Terminator = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");

	if (!MakeWritable(Ini)
		|| !FFileHelper::SaveStringToFile(FString::Join(Lines, Terminator) + Terminator, *Ini,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Notify(FText::Format(LOCTEXT("CommonUiIniReadOnly",
			"Could not write '{0}'. It is read-only, and source control did not check it out."),
			FText::FromString(Ini)), /*bFailed*/ true);
		return;
	}

	FMessageLog Log(FolioMessageLogName);
	Log.NewPage(LOCTEXT("CommonUiPage", "CommonUI"));
	for (const FString& Key : Added)
	{
		Log.Info()->AddToken(FTextToken::Create(FText::Format(
			LOCTEXT("CommonUiSet", "Added {0}=0 to [ConsoleVariables] in DefaultEngine.ini."),
			FText::FromString(Key))));
	}

	Log.Open(EMessageSeverity::Info, /*bForce*/ true);

	Notify(FText::Format(
		LOCTEXT("CommonUiWritten",
			"Set {0} CommonUI {0}|plural(one=variable,other=variables) in DefaultEngine.ini. "
			"Restart the editor: these are read once, on the way up."),
		FText::AsNumber(Added.Num())), /*bFailed*/ false);
}

#undef LOCTEXT_NAMESPACE

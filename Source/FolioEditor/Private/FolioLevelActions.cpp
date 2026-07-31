#include "FolioLevelActions.h"

#include "FolioDocumentPath.h"
#include "FolioMarkerDocument.h"
#include "FolioModule.h"
#include "FolioWorldDocument.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "ScopedTransaction.h"
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

#undef LOCTEXT_NAMESPACE

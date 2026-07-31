#include "FolioLevelActions.h"

#include "FolioDocumentPath.h"
#include "FolioMarkerDocument.h"
#include "FolioModule.h"
#include "FolioWorldDocument.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "Folio"

namespace
{
	/** The issue with a component's document, or empty when there is none. */
	FText DescribeIssue(const UActorComponent& Component)
	{
		FText Message;

		if (const UFolioWorldDocument* World = Cast<UFolioWorldDocument>(&Component))
		{
			UFolioDocumentPathLibrary::DescribeDocumentPathIssue(*World, World->DocumentPath, Message);
		}
		else if (const UFolioMarkerDocument* Marker = Cast<UFolioMarkerDocument>(&Component))
		{
			UFolioDocumentPathLibrary::DescribeDocumentPathIssue(*Marker, Marker->DocumentPath, Message);
		}

		return Message;
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

			const FText Issue = DescribeIssue(*Component);
			if (Issue.IsEmpty())
			{
				continue;
			}

			// Every issue listed, but the actor selected once: one actor can carry several documents
			// and selecting it twice does nothing useful.
			Log.Error()
				->AddToken(FUObjectToken::Create(Actor))
				->AddToken(FTextToken::Create(Issue));

			if (!bAdded)
			{
				Broken.Add(Actor);
				bAdded = true;
			}
		}
	}

	if (Broken.IsEmpty())
	{
		Log.Info()->AddToken(FTextToken::Create(
			LOCTEXT("NoBrokenDocuments", "Every document path in this level resolves.")));
		Log.Open(EMessageSeverity::Info, /*bForce*/ true);
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

	Log.Open(EMessageSeverity::Error, /*bForce*/ true);
}

#undef LOCTEXT_NAMESPACE

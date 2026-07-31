#include "SFolioDocumentTree.h"

#include "FolioDocumentPath.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FolioEditor"

void SFolioDocumentTree::Construct(const FArguments& InArgs)
{
	CurrentPath = InArgs._CurrentPath;
	OnDocumentPicked = InArgs._OnDocumentPicked;

	ChildSlot
	[
		SNew(SBox)
			.WidthOverride(360.0f)
			.MaxDesiredHeight(400.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f)
					[
						SAssignNew(FilterBox, SSearchBox)
							.HintText(LOCTEXT("FilterHint", "Filter documents"))
							.OnTextChanged_Lambda([this](const FText& Text)
							{
								Filter = Text.ToString();
								Rebuild();
							})
					]

				+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SAssignNew(Tree, STreeView<TSharedPtr<FFolioDocumentNode>>)
							.TreeItemsSource(&Roots)
							.OnGenerateRow(this, &SFolioDocumentTree::MakeRow)
							.OnGetChildren(this, &SFolioDocumentTree::GetChildren)
							.OnSelectionChanged(this, &SFolioDocumentTree::OnSelectionChanged)
							.SelectionMode(ESelectionMode::Single)
					]
			]
	];

	Rebuild();
}

TSharedPtr<SWidget> SFolioDocumentTree::GetWidgetToFocusOnOpen() const
{
	return FilterBox;
}

void SFolioDocumentTree::Insert(const FString& Path)
{
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("/"), true);

	if (Segments.IsEmpty())
	{
		return;
	}

	// A mounted path keeps its mount as one segment, so /folio/examples/x.rml groups under "/folio"
	// rather than under an empty root followed by "folio".
	if (Path.StartsWith(TEXT("/")))
	{
		Segments[0] = TEXT("/") + Segments[0];
	}

	TArray<TSharedPtr<FFolioDocumentNode>>* Level = &Roots;

	for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
	{
		const FString& Segment = Segments[Index];

		TSharedPtr<FFolioDocumentNode>* Existing = Level->FindByPredicate(
			[&Segment](const TSharedPtr<FFolioDocumentNode>& Node)
			{
				return Node->IsDirectory() && Node->Label == Segment;
			});

		if (!Existing)
		{
			TSharedPtr<FFolioDocumentNode> Directory = MakeShared<FFolioDocumentNode>();
			Directory->Label = Segment;
			Level->Add(Directory);
			Existing = &(*Level)[Level->Num() - 1];
		}

		Level = &(*Existing)->Children;
	}

	TSharedPtr<FFolioDocumentNode> Document = MakeShared<FFolioDocumentNode>();
	Document->Label = Segments.Last();
	Document->Path = Path;
	Level->Add(Document);
}

void SFolioDocumentTree::Rebuild()
{
	Roots.Reset();

	for (const FString& Path : UFolioDocumentPathLibrary::GetDocumentOptions())
	{
		// Matched against the whole path rather than the file name, so "screens" finds a directory as
		// readily as "hud" finds a document.
		if (Filter.IsEmpty() || Path.Contains(Filter))
		{
			Insert(Path);
		}
	}

	if (!Tree.IsValid())
	{
		return;
	}

	Tree->RequestTreeRefresh();

	if (Filter.IsEmpty())
	{
		ExpandTo(CurrentPath);
	}
	else
	{
		// A match three levels down is no use collapsed, and a filtered tree is short enough that
		// opening all of it is what anyone wants.
		ExpandAll(Roots);
	}
}

void SFolioDocumentTree::ExpandAll(const TArray<TSharedPtr<FFolioDocumentNode>>& Nodes)
{
	for (const TSharedPtr<FFolioDocumentNode>& Node : Nodes)
	{
		if (Node->IsDirectory())
		{
			Tree->SetItemExpansion(Node, true);
			ExpandAll(Node->Children);
		}
	}
}

void SFolioDocumentTree::ExpandTo(const FString& Path)
{
	if (Path.IsEmpty())
	{
		// Nothing chosen yet, so the top level is opened rather than leaving a wall of closed folders.
		for (const TSharedPtr<FFolioDocumentNode>& Node : Roots)
		{
			Tree->SetItemExpansion(Node, true);
		}
		return;
	}

	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("/"), true);
	if (Path.StartsWith(TEXT("/")) && Segments.Num() > 0)
	{
		Segments[0] = TEXT("/") + Segments[0];
	}

	const TArray<TSharedPtr<FFolioDocumentNode>>* Level = &Roots;

	for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
	{
		const TSharedPtr<FFolioDocumentNode>* Directory = Level->FindByPredicate(
			[&Segments, Index](const TSharedPtr<FFolioDocumentNode>& Node)
			{
				return Node->IsDirectory() && Node->Label == Segments[Index];
			});

		if (!Directory)
		{
			return;
		}

		Tree->SetItemExpansion(*Directory, true);
		Level = &(*Directory)->Children;
	}

	if (const TSharedPtr<FFolioDocumentNode>* Selected = Level->FindByPredicate(
		[&Path](const TSharedPtr<FFolioDocumentNode>& Node) { return Node->Path == Path; }))
	{
		Tree->SetSelection(*Selected, ESelectInfo::Direct);
		Tree->RequestScrollIntoView(*Selected);
	}
}

TSharedRef<ITableRow> SFolioDocumentTree::MakeRow(TSharedPtr<FFolioDocumentNode> Node,
	const TSharedRef<STableViewBase>& Owner)
{
	const FSlateBrush* Icon = Node->IsDirectory()
		? FAppStyle::GetBrush(TEXT("Icons.FolderClosed"))
		: FAppStyle::GetBrush(TEXT("Icons.Document"));

	return SNew(STableRow<TSharedPtr<FFolioDocumentNode>>, Owner)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 5.0f, 0.0f)
				[
					SNew(SImage).Image(Icon)
				]

			+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(FText::FromString(Node->Label))
						// A directory is not a choice, so it is drawn quieter than the documents in it.
						.ColorAndOpacity(Node->IsDirectory()
							? FSlateColor::UseSubduedForeground()
							: FSlateColor::UseForeground())
				]
		];
}

void SFolioDocumentTree::GetChildren(TSharedPtr<FFolioDocumentNode> Node,
	TArray<TSharedPtr<FFolioDocumentNode>>& OutChildren)
{
	OutChildren = Node->Children;
}

void SFolioDocumentTree::OnSelectionChanged(TSharedPtr<FFolioDocumentNode> Node, ESelectInfo::Type SelectInfo)
{
	// Direct selections are the ones this widget made itself while opening, so acting on them would
	// commit the value the field already had and close the menu the moment it appeared.
	if (SelectInfo == ESelectInfo::Direct || !Node.IsValid() || Node->IsDirectory())
	{
		return;
	}

	OnDocumentPicked.ExecuteIfBound(Node->Path);
}

#undef LOCTEXT_NAMESPACE

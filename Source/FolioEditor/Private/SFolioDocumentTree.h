#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

/**
 * One entry in the picker: a directory, or a document inside one.
 *
 * Built from the flat list of paths rather than by walking the disk a second time, so what the tree
 * shows and what the property accepts cannot disagree.
 */
struct FFolioDocumentNode
{
	/** The last segment, which is what is drawn. */
	FString Label;

	/** The full document path, empty for a directory. */
	FString Path;

	TArray<TSharedPtr<FFolioDocumentNode>> Children;

	bool IsDirectory() const { return Path.IsEmpty(); }
};

/**
 * A collapsible tree of the documents that exist, with a filter.
 *
 * A flat dropdown stops being useful somewhere around thirty entries, and a game has more markup than
 * that before it has finished its first screen. Directories are the structure an author already has
 * in their head, so the picker uses them rather than inventing a grouping.
 */
class SFolioDocumentTree : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnDocumentPicked, const FString&);

	SLATE_BEGIN_ARGS(SFolioDocumentTree) {}
		/** Expanded to, and highlighted, when the tree opens. */
		SLATE_ARGUMENT(FString, CurrentPath)
		SLATE_EVENT(FOnDocumentPicked, OnDocumentPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Focus lands in the filter box, so typing narrows the list without reaching for the mouse. */
	TSharedPtr<SWidget> GetWidgetToFocusOnOpen() const;

private:
	/** Rebuilds the tree from the current document list and filter. */
	void Rebuild();

	/** Puts one path into the tree, creating whatever directories it needs. */
	void Insert(const FString& Path);

	TSharedRef<ITableRow> MakeRow(TSharedPtr<FFolioDocumentNode> Node, const TSharedRef<STableViewBase>& Owner);
	void GetChildNodes(TSharedPtr<FFolioDocumentNode> Node, TArray<TSharedPtr<FFolioDocumentNode>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FFolioDocumentNode> Node, ESelectInfo::Type SelectInfo);

	/** Expands every directory on the way to the current path, so the tree opens showing it. */
	void ExpandTo(const FString& Path);

	/** Expands everything, which is what a filtered tree wants: a match three levels down is no use collapsed. */
	void ExpandAll(const TArray<TSharedPtr<FFolioDocumentNode>>& Nodes);

	TArray<TSharedPtr<FFolioDocumentNode>> Roots;
	TSharedPtr<STreeView<TSharedPtr<FFolioDocumentNode>>> Tree;
	TSharedPtr<class SSearchBox> FilterBox;

	FString Filter;
	FString CurrentPath;
	FOnDocumentPicked OnDocumentPicked;
};

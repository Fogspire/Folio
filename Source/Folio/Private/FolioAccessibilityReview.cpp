#include "FolioAccessibilityReview.h"

#include "FolioAccessibility.h"
#include "FolioAccessibilityText.h"
#include "FolioDocumentHost.h"
#include "FolioRmlUi.h"
#include "FolioSpeech.h"

#include "FolioCore/FolioSemantics.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	/** One thing the cursor can stand on: a node of some document's semantic tree, flattened. */
	struct FReviewItem
	{
		Rml::Element* Element = nullptr;
		Folio::FSemanticNode Node;
		int32 Depth = 0;

		/** The nearest named group above it, said on the way in and not on every row after. */
		FString Group;
	};

	/**
	 * The cursor, held between calls.
	 *
	 * The list is not: it is rebuilt on every command, because a document can be reloaded, closed or
	 * rewritten by a binding between one keypress and the next, and a cached tree would be a cursor
	 * standing on an element that no longer exists. Rebuilding is one walk per open document and only
	 * happens when a player asks for something.
	 */
	struct FReviewState
	{
		Rml::ObserverPtr<Rml::Element> At;
		FString Group;
	};

	FReviewState& Review()
	{
		static FReviewState Instance;
		return Instance;
	}

	void Flatten(const Folio::FSemanticNode& Node, const int32 Depth, const FString& Group,
		TArray<FReviewItem>& Out)
	{
		// The root is the document itself. It carries the screen's name, which is worth reaching - it
		// is what tells a player which screen they are on - so it goes in like anything else.
		FReviewItem Item;
		Item.Element = Node.Element;
		Item.Node = Node;
		Item.Node.Children.clear();
		Item.Depth = Depth;
		Item.Group = Group;

		const bool bNamedGroup = Node.Role == Folio::ESemanticRole::Group && !Node.Name.empty();
		const FString Inner = bNamedGroup ? Folio::ToFString(Node.Name) : Group;

		Out.Add(MoveTemp(Item));

		for (const Folio::FSemanticNode& Child : Node.Children)
		{
			Flatten(Child, Depth + 1, Inner, Out);
		}
	}

	/**
	 * Everything on screen, in reading order, newest document first.
	 *
	 * Every live document rather than every canvas: a document drawn in the world has no Slate widget
	 * and no platform reader can see it, which makes this the only thing that can read one out.
	 */
	TArray<FReviewItem> Collect()
	{
		TArray<FReviewItem> Items;

		const TArray<FFolioDocumentHost*>& Hosts = FFolioDocumentHost::GetLiveHosts();
		for (int32 Index = Hosts.Num() - 1; Index >= 0; --Index)
		{
			Rml::ElementDocument* Document = Hosts[Index]->GetDocument();
			if (!Document || !Hosts[Index]->IsLaidOut())
			{
				continue;
			}

			Flatten(Folio::BuildSemanticTree(Document), 0, FString(), Items);
		}

		return Items;
	}

	/** Where the cursor is in a list that has just been rebuilt, or INDEX_NONE if it has gone. */
	int32 IndexOfCursor(const TArray<FReviewItem>& Items)
	{
		Rml::Element* At = Review().At.get();
		if (!At)
		{
			return INDEX_NONE;
		}

		return Items.IndexOfByPredicate([At](const FReviewItem& Item) { return Item.Element == At; });
	}

	/** A group name only when it is not the one already said, which is what stops every row repeating it. */
	FString LineFor(const FReviewItem& Item, const bool bWithGroup)
	{
		const FString Line = Folio::Utterance(Item.Node);
		if (Line.IsEmpty() || !bWithGroup || Item.Group.IsEmpty())
		{
			return Line;
		}

		return Folio::Spoken::Join({Item.Group, Line});
	}

	FString Land(const TArray<FReviewItem>& Items, const int32 Index, const bool bInterrupt)
	{
		if (!Items.IsValidIndex(Index))
		{
			return FString();
		}

		const FReviewItem& Item = Items[Index];

		FReviewState& State = Review();
		State.At = Item.Element ? Item.Element->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();

		const bool bWithGroup = Item.Group != State.Group;
		State.Group = Item.Group;

		const FString Line = LineFor(Item, bWithGroup);
		FFolioAccessibilityTree::SpeakLine(Line, bInterrupt);
		return Line;
	}
}

FString FFolioReview::Move(const EMove InMove)
{
	if (InMove == EMove::Repeat)
	{
		const FString Line = FFolioAccessibilityTree::GetLastSpoken();
		Folio::FSpeech::Say(Line, /*bInterrupt*/ true);
		return Line;
	}

	const TArray<FReviewItem> Items = Collect();
	if (Items.IsEmpty())
	{
		const FString Line = Folio::Spoken::NothingOnScreen().ToString();
		FFolioAccessibilityTree::SpeakLine(Line, /*bInterrupt*/ true);
		return Line;
	}

	const int32 Current = IndexOfCursor(Items);

	int32 Next = 0;
	switch (InMove)
	{
	case EMove::First: Next = 0; break;
	case EMove::Last: Next = Items.Num() - 1; break;

	// From nowhere, forwards starts at the top and backwards at the bottom, so a player who has not
	// used the cursor yet gets a whole screen from either key rather than nothing from one of them.
	case EMove::Next: Next = Current == INDEX_NONE ? 0 : Current + 1; break;
	case EMove::Previous: Next = Current == INDEX_NONE ? Items.Num() - 1 : Current - 1; break;
	default: return FString();
	}

	// Skipped rather than stopped on: a layout div with nothing to say is not somewhere a player
	// should have to press past, and there are usually several between any two things worth hearing.
	const int32 Step = InMove == EMove::Previous || InMove == EMove::Last ? -1 : 1;
	while (Items.IsValidIndex(Next) && Folio::Utterance(Items[Next].Node).IsEmpty())
	{
		Next += Step;
	}

	if (!Items.IsValidIndex(Next))
	{
		// The edge is said rather than passed over in silence: a key that does nothing is
		// indistinguishable from a key that is not bound.
		const FString Edge = (Next < 0 ? Folio::Spoken::TopOfScreen() : Folio::Spoken::EndOfScreen())
			.ToString();

		FFolioAccessibilityTree::SpeakLine(Edge, /*bInterrupt*/ true);
		return Edge;
	}

	return Land(Items, Next, /*bInterrupt*/ true);
}

FString FFolioReview::ReadAll()
{
	const TArray<FReviewItem> Items = Collect();
	if (Items.IsEmpty())
	{
		const FString Line = Folio::Spoken::NothingOnScreen().ToString();
		FFolioAccessibilityTree::SpeakLine(Line, /*bInterrupt*/ true);
		return Line;
	}

	const int32 Start = FMath::Max(0, IndexOfCursor(Items));

	TArray<FString> Spoken;
	FReviewState& State = Review();

	for (int32 Index = Start; Index < Items.Num(); ++Index)
	{
		const FReviewItem& Item = Items[Index];

		const bool bWithGroup = Item.Group != State.Group;
		State.Group = Item.Group;

		if (const FString Line = LineFor(Item, bWithGroup); !Line.IsEmpty())
		{
			// The first interrupts and the rest queue behind it, which is what makes this one reading
			// rather than a burst of lines each cancelling the one before.
			FFolioAccessibilityTree::SpeakLine(Line, Spoken.IsEmpty());
			Spoken.Add(Line);
		}

		State.At = Item.Element ? Item.Element->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();
	}

	return FString::Join(Spoken, TEXT("\n"));
}

void FFolioReview::Stop()
{
	Folio::FSpeech::Silence();
}

bool FFolioReview::Activate()
{
	Rml::Element* At = Review().At.get();
	if (!At)
	{
		return false;
	}

	// Focused first, as the tree's own activate does: everything a document does on focus should
	// happen for a player who arrived by reviewing rather than by navigating.
	At->Focus(true);
	At->Click();
	return true;
}

bool FFolioReview::MoveFocusHere()
{
	Rml::Element* At = Review().At.get();
	return At && At->Focus(true);
}

void FFolioReview::FollowFocus(Rml::Element* Element)
{
	FReviewState& State = Review();
	State.At = Element ? Element->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();

	// The group is deliberately not cleared. The focus change is about to be spoken with its own
	// group prefix, and clearing it here would have the next review step say the panel name again.
}

void FFolioReview::Describe(TArray<FString>& Out)
{
	const TArray<FReviewItem> Items = Collect();
	Rml::Element* At = Review().At.get();

	for (const FReviewItem& Item : Items)
	{
		const FString Line = Folio::Utterance(Item.Node);
		if (Line.IsEmpty())
		{
			continue;
		}

		Out.Add(FString::Printf(TEXT("%s%s%s"),
			Item.Element == At ? TEXT("> ") : TEXT("  "),
			*FString::ChrN(Item.Depth * 2, TEXT(' ')), *Line));
	}
}

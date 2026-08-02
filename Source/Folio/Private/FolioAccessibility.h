#pragma once

#include "FolioCore/FolioSemantics.h"

#include "CoreMinimal.h"
#include "Layout/Children.h"
#include "Widgets/SPanel.h"

#if WITH_ACCESSIBILITY
#include "Widgets/Accessibility/SlateCoreAccessibleWidgets.h"
#endif

namespace Rml
{
	class Context;
	class ElementDocument;
}

/**
 * A document, made reachable by a screen reader.
 *
 * Folio draws through one Slate widget, so NVDA, JAWS and console TTS see one rectangle. Unreal's
 * accessibility tree is built by walking `SWidget::GetChildren()` from the window down, and
 * `FSlateAccessibleWidget`'s parent, child and bounds accessors are all `final` and all read from that
 * walk - so there is no standalone tree to hand over. What there is instead is this: one widget per
 * thing in the document worth announcing, drawing nothing, hit-test invisible, arranged where its
 * element is. Slate then builds exactly the tree the document describes, out of widgets it already
 * knows how to enumerate.
 *
 * They exist only while something is listening. A screen reader turning on is what puts them there,
 * and until then a document costs one widget as before.
 *
 * What they are built from is Folio::BuildSemanticTree, which is also what `uipreview --a11y` checks
 * and `--a11y-tree` prints. One walk, so a document that reads correctly in the tool reads correctly
 * to a player.
 */
class SFolioAccessibleElement : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SFolioAccessibleElement) {}
	SLATE_END_ARGS()

	SFolioAccessibleElement();

	void Construct(const FArguments& InArgs, Folio::ESemanticRole InRole);

	/** Re-reads name, value and geometry from the element. The role never changes; a new one is a new widget. */
	void Update(const Folio::FSemanticNode& InNode, float InContextToLocal);

	/** Replaces the children, which is what a document changing shape does. */
	void SetAccessibleChildren(TArray<TSharedRef<SFolioAccessibleElement>>&& InChildren);

	bool HasSameChildren(const TArray<TSharedRef<SFolioAccessibleElement>>& Other) const;

	int32 NumAccessibleChildren() const { return Children.Num(); }
	const TSharedRef<SFolioAccessibleElement>& GetAccessibleChild(int32 Index) const { return Children[Index]; }

	Folio::ESemanticRole GetRole() const { return Role; }
	Rml::Element* GetElement() const { return Element.get(); }
	const Folio::FSemanticNode& GetNode() const { return Node; }
	const FString& GetValueText() const { return ValueText; }
	const FText& GetName() const { return Name; }

	/** One line of the tree, for folio.Accessibility.Dump. */
	FString Describe() const;

	/** The document was driven from outside its own input, so the host has to repaint. */
	FSimpleDelegate OnChanged;

	//~ SWidget
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FChildren* GetChildren() override;

	/**
	 * Never. The canvas is the focus target and RmlUi navigates inside it; a proxy that could hold
	 * Slate focus would put the two navigations in a fight neither can win.
	 */
	virtual bool SupportsKeyboardFocus() const override { return false; }

#if WITH_ACCESSIBILITY
	virtual TSharedRef<FSlateAccessibleWidget> CreateAccessibleWidget() override;
#endif
	//~

	/** Presses the element, which is what a screen reader's activate does. */
	void Activate();

	/**
	 * Whether a name arriving on this node counts as news.
	 *
	 * False for the tree built when a screen reader starts, or every `folio-announce` element on
	 * screen would announce itself at once. True for anything appearing after that, which is what a
	 * status line being written to is.
	 */
	void SetAnnouncesOnArrival(bool bInAnnounces) { bAnnouncesOnArrival = bInAnnounces; }

	/** Writes a value back, for the platforms that let a user set one. */
	void SetValueFromPlatform(const FString& Value);

private:
	Folio::ESemanticRole Role = Folio::ESemanticRole::None;

	/** Observed rather than held: the document owns its elements and can free them under us. */
	Rml::ObserverPtr<Rml::Element> Element;

	/** The node as it was last read, with its children stripped - those are widgets instead. */
	Folio::FSemanticNode Node;

	FText Name;
	FString ValueText;

	/** How many Slate local units one context unit is, which is what the canvas lays out against. */
	float ContextToLocal = 1.0f;

	bool bAnnouncesOnArrival = false;
	bool bUpdatedOnce = false;

	TSlotlessChildren<SFolioAccessibleElement> Children;
};

/**
 * The tree for one document, and the bookkeeping that keeps it honest.
 *
 * Rebuilt from the document rather than patched by events, because RmlUi has no change notification
 * that covers everything a document can do to itself and a tree that is silently one edit behind is
 * worse than no tree. Rebuilding only happens on a frame the document itself changed, and only while
 * a screen reader is listening.
 */
class FFolioAccessibilityTree
{
public:
	/** Whether anything is listening. False is the normal case and costs one branch a frame. */
	static bool IsWanted();

	/**
	 * Says something out loud, outside the tree.
	 *
	 * For what a document cannot express by existing: a save completing, a round starting, a warning
	 * that appears and is gone before anyone navigates to it. Markup asks for the same thing with
	 * `folio-announce`; this is the seam for game code that has no element to hang it on.
	 */
	static void Announce(const FString& Text);

	/** Rebuilds from the document. Returns true when the widget tree changed shape. */
	bool Refresh(Rml::ElementDocument* Document, float ContextToLocal);

	/** Drops everything, which is what a document closing or a screen reader stopping does. */
	void Clear();

	/** Tells the platform what has focus, so navigating a document reads the control moved onto. */
	void SyncFocus(Rml::Context* Context);

	bool IsEmpty() const { return !Root.IsValid(); }
	TSharedPtr<SFolioAccessibleElement> GetRoot() const { return Root; }

	/** The whole tree as indented lines, for folio.Accessibility.Dump. */
	void Describe(TArray<FString>& Out) const;

	/** Bound to the host, so a screen reader pressing something repaints the document. */
	FSimpleDelegate OnChanged;

private:
	TSharedRef<SFolioAccessibleElement> Sync(const Folio::FSemanticNode& Node, float ContextToLocal,
		const TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>>& Existing, bool& bOutChanged);

	static void Collect(const TSharedPtr<SFolioAccessibleElement>& At,
		TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>>& Out);

	TSharedPtr<SFolioAccessibleElement> Root;

	/** False until the first build has happened, which is what stops it announcing the whole screen. */
	bool bBuiltOnce = false;

	/** Every live proxy by its element, for the focus lookup. */
	TMap<Rml::Element*, TWeakPtr<SFolioAccessibleElement>> ByElement;

	/** What was last reported as focused, so the same element is not announced twice. */
	Rml::Element* LastFocused = nullptr;
};

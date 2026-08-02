#pragma once

#include "FolioCore/FolioSemantics.h"
#include "FolioSpeech.h"

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

namespace Folio
{
	/**
	 * One node as a sentence: its name, what kind of thing it is, then what state it is in.
	 *
	 * The order every screen reader uses, and the one place it is decided - the tree speaks focus
	 * changes through this and the review cursor speaks everything else through it, so a control does
	 * not read two different ways depending on how the player arrived at it.
	 */
	FString Utterance(const FSemanticNode& Node);
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

	/**
	 * Re-reads only where the element is, which is all that changes on most frames.
	 *
	 * One box query per node and no strings, so a HUD animating under a screen reader costs a read
	 * rather than a walk. False when the element has been freed, which is what tells the tree it is
	 * out of date.
	 */
	bool SyncGeometry(float InContextToLocal);

	/** Replaces the children, which is what a document changing shape does. */
	void SetAccessibleChildren(TArray<TSharedRef<SFolioAccessibleElement>>&& InChildren);

	bool HasSameChildren(const TArray<TSharedRef<SFolioAccessibleElement>>& Other) const;

	int32 NumAccessibleChildren() const { return Children.Num(); }
	const TSharedRef<SFolioAccessibleElement>& GetAccessibleChild(int32 Index) const { return Children[Index]; }

	/**
	 * The proxy above this one, or nothing at the root.
	 *
	 * Kept rather than read back from `GetParentWidget`, because the widget above the root belongs to
	 * whatever put the canvas on screen and is not one of these.
	 */
	TSharedPtr<SFolioAccessibleElement> GetAccessibleParent() const { return AccessibleParent.Pin(); }

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

	TWeakPtr<SFolioAccessibleElement> AccessibleParent;

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
	 * Holds the tree open for something that is not a screen reader.
	 *
	 * Counted by reason rather than by a bool, so two systems asking do not cancel one another and a
	 * request nobody released can be named in a dump instead of being a mystery cost.
	 */
	static void AddRequest(FName Reason);
	static void RemoveRequest(FName Reason);
	static void DescribeRequests(TArray<FString>& Out);

	/**
	 * The word or the line the caret is in, and the word spelled out.
	 *
	 * Typing is echoed as it happens; this is the other half, which every real reader has and which a
	 * player needs the moment they stop typing and start correcting. Nothing moves - the caret is where
	 * it was afterwards.
	 *
	 * Empty when nothing is being edited. A password field spells stars, for the same reason it echoes
	 * them.
	 */
	static FString ReadEditWord();
	static FString ReadEditLine();
	static FString SpellEditWord();

	/**
	 * Turns the game's own screen reader on or off.
	 *
	 * Two different players are covered by one tree. One runs NVDA or a console narrator, and needs
	 * only the tree - the platform reads it and this stays off. The other has no screen reader,
	 * because they are on a console where there is nothing to run, or because they expect the game to
	 * read itself; for them the tree is walked all the same and Folio speaks the focused control.
	 *
	 * A game setting drives this. It is never on by default: a menu that starts talking is a bug for
	 * everyone who did not ask for it.
	 */
	static void SetScreenReader(bool bEnabled);
	static bool IsScreenReaderEnabled();

	/**
	 * Says something out loud, outside the tree.
	 *
	 * For what a document cannot express by existing: a save completing, a round starting, a warning
	 * that appears and is gone before anyone navigates to it. Markup asks for the same thing with
	 * `folio-announce`; this is the seam for game code that has no element to hang it on.
	 */
	static void Announce(const FString& Text);

	/**
	 * Says one line and remembers it, so the review cursor's repeat has something to repeat.
	 *
	 * Everything the reader says goes through here rather than straight to the voice, including the
	 * lines the review cursor produces. A player who missed a sentence should get that sentence back
	 * whichever part of the reader produced it.
	 */
	static void SpeakLine(const FString& Line, Folio::ESpeechPriority Priority,
		FName Channel = NAME_None);
	static const FString& GetLastSpoken();

	/**
	 * Rebuilds from the document. Returns true when the widget tree changed shape.
	 *
	 * Rate limited: a HUD that animates every frame would otherwise pay a walk every frame for a
	 * document whose *meaning* changes once a minute. Between rebuilds the proxies still follow their
	 * elements, because that is a read per node rather than a walk. `bForce` is for the cases where a
	 * frame of latency is wrong - a screen reader starting, and focus landing on something new.
	 */
	bool Refresh(Rml::ElementDocument* Document, float ContextToLocal, bool bForce = false);

	/** Drops everything, which is what a document closing or a screen reader stopping does. */
	void Clear();

	/** Tells the platform what has focus, so navigating a document reads the control moved onto. */
	void SyncFocus(Rml::Context* Context);

	/**
	 * Reads back what is being typed into the focused field.
	 *
	 * Nothing else covers this. A text field's contents are one accessible value, and a value changing
	 * is not an event a screen reader announces - a player typing their name into an empty box would
	 * otherwise hear nothing at all from the first keystroke to the last. Only the game's own reader
	 * does this: a platform reader has its own character echo, and doing it here too would double
	 * every letter.
	 *
	 * Run every frame rather than on document change, because a caret moving is not a change to the
	 * document and still has to be read out.
	 */
	void SyncEditing(Rml::Context* Context);

	bool IsEmpty() const { return !Root.IsValid(); }
	TSharedPtr<SFolioAccessibleElement> GetRoot() const { return Root; }

	/** The whole tree as indented lines, for folio.Accessibility.Dump. */
	void Describe(TArray<FString>& Out) const;

	/** Bound to the host, so a screen reader pressing something repaints the document. */
	FSimpleDelegate OnChanged;

private:
	/** Reads a control out, with the panel it sits in when that has just changed. */
	void Speak(const TSharedRef<SFolioAccessibleElement>& Proxy);

	TSharedRef<SFolioAccessibleElement> Sync(const Folio::FSemanticNode& Node, float ContextToLocal,
		const TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>>& Existing, bool& bOutChanged);

	static void Collect(const TSharedPtr<SFolioAccessibleElement>& At,
		TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>>& Out);

	TSharedPtr<SFolioAccessibleElement> Root;

	/** False until the first build has happened, which is what stops it announcing the whole screen. */
	bool bBuiltOnce = false;

	/** When the tree was last walked, against which the rate limit is measured. */
	double LastRebuild = 0.0;

	/** Set when something asked about an element the tree has never heard of. Forces the next rebuild. */
	bool bStale = false;

	/** Every live proxy by its element, for the focus lookup. */
	TMap<Rml::Element*, TWeakPtr<SFolioAccessibleElement>> ByElement;

	/** What was last reported as focused, so the same element is not announced twice. */
	Rml::Element* LastFocused = nullptr;

	/** The group whose name was last spoken, so moving within a panel does not re-announce it. */
	FString SpokenGroup;

	/**
	 * The focused text field as it was last read out.
	 *
	 * Observed rather than held, because the field can be freed by a reload while this still points at
	 * it, and a caret index into a string that no longer exists is the sort of bug that only shows up
	 * on a machine with the reader turned on.
	 */
	Rml::ObserverPtr<Rml::Element> EditElement;
	FString EditText;
	int32 EditCaret = 0;
	int32 EditSelectionEnd = 0;
};

#include "FolioAccessibility.h"

#include "FolioAccessibilityLibrary.h"
#include "FolioAccessibilityReview.h"
#include "FolioAccessibilityText.h"
#include "FolioModule.h"
#include "FolioPresentation.h"
#include "FolioRmlUi.h"
#include "FolioSpeech.h"

#include "HAL/IConsoleManager.h"
#include "Layout/ArrangedChildren.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>

#if WITH_ACCESSIBILITY
#include "Application/SlateApplicationBase.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif

namespace
{
	TAutoConsoleVariable<int32> CVarForceAccessibility(
		TEXT("folio.Accessibility.Force"), 0,
		TEXT("Builds the screen reader tree for every document even with no screen reader running. "
			"For folio.Accessibility.Dump, and for measuring what it costs."),
		ECVF_Default);

	/** Whether the game is reading itself out. A game setting owns this; nothing turns it on by itself. */
	bool GScreenReader = false;

	/** The last thing said, whichever part of the reader said it. What review's repeat repeats. */
	FString GLastSpoken;

	/** The nearest named group above a control, which is the context a player has just moved into. */
	FString GroupOf(const SFolioAccessibleElement& Proxy)
	{
		for (TSharedPtr<SFolioAccessibleElement> At = Proxy.GetAccessibleParent(); At.IsValid();
			At = At->GetAccessibleParent())
		{
			if (At->GetRole() == Folio::ESemanticRole::Group && !At->GetName().IsEmpty())
			{
				return At->GetName().ToString();
			}
		}

		return FString();
	}

#if WITH_ACCESSIBILITY
	/**
	 * What the platform is told this is.
	 *
	 * A progress bar becomes a slider, which is the only thing in Unreal's list that carries a value
	 * and a range: read out as "slider, 40" rather than silently, which is what an Unknown gets.
	 */
	EAccessibleWidgetType ToAccessibleType(const Folio::ESemanticRole Role)
	{
		switch (Role)
		{
		case Folio::ESemanticRole::Group: return EAccessibleWidgetType::Layout;
		case Folio::ESemanticRole::Text: return EAccessibleWidgetType::Text;
		case Folio::ESemanticRole::Image: return EAccessibleWidgetType::Image;
		case Folio::ESemanticRole::Button: return EAccessibleWidgetType::Button;
		case Folio::ESemanticRole::Link: return EAccessibleWidgetType::Hyperlink;
		case Folio::ESemanticRole::CheckBox: return EAccessibleWidgetType::CheckBox;
		case Folio::ESemanticRole::Radio: return EAccessibleWidgetType::CheckBox;
		case Folio::ESemanticRole::ComboBox: return EAccessibleWidgetType::ComboBox;
		case Folio::ESemanticRole::Slider: return EAccessibleWidgetType::Slider;
		case Folio::ESemanticRole::ProgressBar: return EAccessibleWidgetType::Slider;
		case Folio::ESemanticRole::TextEdit: return EAccessibleWidgetType::TextEdit;
		case Folio::ESemanticRole::List: return EAccessibleWidgetType::List;
		case Folio::ESemanticRole::ListItem: return EAccessibleWidgetType::ListItem;
		default: return EAccessibleWidgetType::Unknown;
		}
	}

	/**
	 * One node of the document, as the platform's accessibility API sees it.
	 *
	 * Every interface at once rather than a class per role, because the roles here are a document's
	 * roles rather than Slate's: the same widget is a checkbox in one document and a slider in the
	 * next, and the As* answers follow the node it is currently showing.
	 */
	class FFolioAccessibleElement
		: public FSlateAccessibleWidget
		, public IAccessibleActivatable
		, public IAccessibleProperty
		, public IAccessibleText
	{
	public:
		FFolioAccessibleElement(TWeakPtr<SWidget> InWidget, const EAccessibleWidgetType InType)
			: FSlateAccessibleWidget(InWidget, InType)
		{
		}

		virtual IAccessibleActivatable* AsActivatable() override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy && Proxy->GetNode().Interactive ? this : nullptr;
		}

		virtual IAccessibleProperty* AsProperty() override
		{
			const SFolioAccessibleElement* Proxy = Get();
			if (!Proxy)
			{
				return nullptr;
			}

			switch (Proxy->GetRole())
			{
			case Folio::ESemanticRole::CheckBox:
			case Folio::ESemanticRole::Radio:
			case Folio::ESemanticRole::ComboBox:
			case Folio::ESemanticRole::Slider:
			case Folio::ESemanticRole::ProgressBar:
			case Folio::ESemanticRole::TextEdit:
				return this;
			default:
				return nullptr;
			}
		}

		virtual IAccessibleText* AsText() override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy && Proxy->GetRole() == Folio::ESemanticRole::TextEdit ? this : nullptr;
		}

		/**
		 * The name, and nothing invented when there is none.
		 *
		 * The base class falls back to the widget's class name, which would have a screen reader read
		 * "SFolioAccessibleElement" out to a player. An unnamed control reading as its role alone is
		 * the honest failure, and it is what `--a11y` reports so it does not survive to a build.
		 */
		virtual FString GetWidgetName() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Proxy->GetName().ToString() : FString();
		}

		virtual FString GetHelpText() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Folio::ToFString(Proxy->GetNode().Description) : FString();
		}

		// IAccessibleActivatable
		virtual void Activate() override
		{
			if (SFolioAccessibleElement* Proxy = Get())
			{
				Proxy->Activate();
			}
		}

		virtual bool IsCheckable() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy && Proxy->GetNode().Checkable;
		}

		virtual bool GetCheckedState() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy && Proxy->GetNode().Checked;
		}
		// ~

		// IAccessibleProperty
		virtual bool IsReadOnly() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return !Proxy || Proxy->GetNode().ReadOnly;
		}

		virtual bool IsPassword() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy && Proxy->GetNode().Password;
		}

		virtual float GetStepSize() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Proxy->GetNode().Step : 0.0f;
		}

		virtual float GetMaximum() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Proxy->GetNode().Maximum : 0.0f;
		}

		virtual float GetMinimum() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Proxy->GetNode().Minimum : 0.0f;
		}

		virtual FString GetValue() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Proxy->GetValueText() : FString();
		}

		virtual FVariant GetValueAsVariant() const override
		{
			const SFolioAccessibleElement* Proxy = Get();
			if (Proxy && Proxy->GetNode().HasRange)
			{
				return FVariant(FCString::Atof(*Proxy->GetValueText()));
			}

			return FVariant(GetValue());
		}

		virtual void SetValue(const FString& Value) override
		{
			if (SFolioAccessibleElement* Proxy = Get())
			{
				Proxy->SetValueFromPlatform(Value);
			}
		}
		// ~

		// IAccessibleText
		virtual const FString& GetText() const override
		{
			static const FString Empty;
			const SFolioAccessibleElement* Proxy = Get();
			return Proxy ? Proxy->GetValueText() : Empty;
		}
		// ~

	private:
		SFolioAccessibleElement* Get() const
		{
			return Widget.IsValid() ? static_cast<SFolioAccessibleElement*>(Widget.Pin().Get()) : nullptr;
		}
	};
#endif // WITH_ACCESSIBILITY
}

FString Folio::Utterance(const Folio::FSemanticNode& Node)
{
	TArray<FString> Parts;

	if (!Node.Name.empty())
	{
		Parts.Add(Folio::ToFString(Node.Name));
	}

	if (const FText Role = Folio::Spoken::Role(Node.Role); !Role.IsEmpty())
	{
		Parts.Add(Role.ToString());
	}

	if (Node.Checkable)
	{
		Parts.Add(Folio::Spoken::Checked(Node.Checked).ToString());
	}

	if (!Node.Value.empty() && Node.Role != Folio::ESemanticRole::Text)
	{
		// A range's value is digits the document produced, so it is read as this culture writes a
		// number. Anything else is already words and is read as it stands.
		Parts.Add(Node.HasRange
			? Folio::Spoken::Number(Folio::ToFString(Node.Value)).ToString()
			: Folio::ToFString(Node.Value));
	}

	// A control that is present and cannot be used. Said last, because it qualifies everything before
	// it, and before nothing, because there is nothing a player can do about it.
	if (Node.Disabled)
	{
		Parts.Add(Folio::Spoken::Unavailable().ToString());
	}

	return Folio::Spoken::Join(Parts);
}

SFolioAccessibleElement::SFolioAccessibleElement()
	: Children(this)
{
}

void SFolioAccessibleElement::Construct(const FArguments&, const Folio::ESemanticRole InRole)
{
	Role = InRole;

	// Hit-test invisible, because the document is what the pointer talks to. A proxy that answered a
	// hit test would sit between the cursor and the element it stands for.
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);

#if WITH_ACCESSIBILITY
	// Setting a behaviour tells the message handler the tree changed, so it needs one to tell.
	if (FSlateApplicationBase::IsInitialized())
	{
		SetAccessibleBehavior(EAccessibleBehavior::Custom, FText::GetEmpty(), EAccessibleType::Main);
	}
#endif
}

void SFolioAccessibleElement::Update(const Folio::FSemanticNode& InNode, const float InContextToLocal)
{
	Element = InNode.Element ? InNode.Element->GetObserverPtr() : Rml::ObserverPtr<Rml::Element>();

	// The children are widgets rather than nodes, and holding a second copy of the subtree here would
	// be a second thing to keep in step with the document.
	Node = InNode;
	Node.Children.clear();

	ContextToLocal = InContextToLocal;
	ValueText = Folio::ToFString(InNode.Value);

	if (const FString NewName = Folio::ToFString(InNode.Name); NewName != Name.ToString())
	{
		const bool bNews = bUpdatedOnce || bAnnouncesOnArrival;

		Name = FText::FromString(NewName);
#if WITH_ACCESSIBILITY
		if (FSlateApplicationBase::IsInitialized())
		{
			SetAccessibleBehavior(EAccessibleBehavior::Custom, Name, EAccessibleType::Main);
		}
#endif

		// A line that rewrites itself is read out where the player is, rather than where it is. Without
		// this a warning arriving in the corner of a HUD reaches nobody: a screen reader only reads
		// what it is standing on, and nobody stands on a status line.
		if (Node.Announces && bNews && !NewName.IsEmpty())
		{
			FFolioAccessibilityTree::Announce(NewName);
		}
	}

	bUpdatedOnce = true;
}

void SFolioAccessibleElement::SetAccessibleChildren(TArray<TSharedRef<SFolioAccessibleElement>>&& InChildren)
{
	Children.Empty(InChildren.Num());
	for (const TSharedRef<SFolioAccessibleElement>& Child : InChildren)
	{
		Child->AccessibleParent = SharedThis(this);
		Children.Add(Child);
	}
}

bool SFolioAccessibleElement::HasSameChildren(const TArray<TSharedRef<SFolioAccessibleElement>>& Other) const
{
	if (Children.Num() != Other.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Other.Num(); ++Index)
	{
		if (Children[Index] != Other[Index])
		{
			return false;
		}
	}

	return true;
}

FString SFolioAccessibleElement::Describe() const
{
	FString Line = ANSI_TO_TCHAR(Folio::RoleName(Role));

	if (!Name.IsEmpty())
	{
		Line += FString::Printf(TEXT(" \"%s\""), *Name.ToString());
	}
	else if (Node.Interactive)
	{
		Line += TEXT(" <unnamed>");
	}

	if (!ValueText.IsEmpty() && Role != Folio::ESemanticRole::Text)
	{
		Line += TEXT(" = ") + ValueText;
	}

	if (Node.Checkable)
	{
		Line += Node.Checked ? TEXT(" [checked]") : TEXT(" [unchecked]");
	}

	if (Node.Disabled)
	{
		Line += TEXT(" [disabled]");
	}

	return Line;
}

void SFolioAccessibleElement::OnArrangeChildren(const FGeometry& AllottedGeometry,
	FArrangedChildren& ArrangedChildren) const
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const TSharedRef<SFolioAccessibleElement>& Child = Children[Index];
		if (!ArrangedChildren.Accepts(Child->GetVisibility()))
		{
			continue;
		}

		// The document lays out in absolute context units, so a child sits at the difference between
		// the two elements' offsets rather than anywhere Slate has an opinion about.
		const Rml::Vector2f Offset = Child->Node.Offset - Node.Offset;

		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Child,
			FVector2D(Child->Node.Size.x * ContextToLocal, Child->Node.Size.y * ContextToLocal),
			FSlateLayoutTransform(FVector2D(Offset.x * ContextToLocal, Offset.y * ContextToLocal))));
	}
}

FVector2D SFolioAccessibleElement::ComputeDesiredSize(float) const
{
	return FVector2D(Node.Size.x * ContextToLocal, Node.Size.y * ContextToLocal);
}

FChildren* SFolioAccessibleElement::GetChildren()
{
	return &Children;
}

#if WITH_ACCESSIBILITY
TSharedRef<FSlateAccessibleWidget> SFolioAccessibleElement::CreateAccessibleWidget()
{
	return MakeShared<FFolioAccessibleElement>(SharedThis(this), ToAccessibleType(Role));
}
#endif

void SFolioAccessibleElement::Activate()
{
	Rml::Element* Target = Element.get();
	if (!Target)
	{
		return;
	}

	// Focused first: a screen reader activating a control is a player pressing it, and everything a
	// document does on focus - a description panel, a preview, a sound - should happen for them too.
	Target->Focus(true);
	Target->Click();

	OnChanged.ExecuteIfBound();
}

void SFolioAccessibleElement::SetValueFromPlatform(const FString& Value)
{
	Rml::Element* Target = Element.get();
	if (!Target || Node.ReadOnly)
	{
		return;
	}

	if (Rml::ElementFormControl* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Target))
	{
		Control->SetValue(Folio::ToRmlString(Value));
		OnChanged.ExecuteIfBound();
	}
}

//
// The tree
//

void FFolioAccessibilityTree::SetScreenReader(const bool bEnabled)
{
	if (GScreenReader == bEnabled)
	{
		return;
	}

	GScreenReader = bEnabled;

	if (bEnabled)
	{
		GScreenReader = Folio::FSpeech::Start();
		if (GScreenReader)
		{
			// The player has just turned this on and needs to know it took, before anything they do
			// next produces a sentence they might mistake for the confirmation.
			SpeakLine(Folio::Spoken::ReaderOn().ToString(), /*bInterrupt*/ true);
		}
	}
	else
	{
		// Silence is the confirmation. Saying "off" would mean holding the voice open to finish a
		// sentence about having stopped talking.
		Folio::FSpeech::Stop();
	}
}

bool FFolioAccessibilityTree::IsScreenReaderEnabled()
{
	return GScreenReader;
}

bool FFolioAccessibilityTree::IsWanted()
{
	if (GScreenReader || CVarForceAccessibility.GetValueOnGameThread() != 0)
	{
		return true;
	}

#if WITH_ACCESSIBILITY
	// IsActive is the platform saying something is listening - on Windows, that a screen reader has
	// asked the window for an accessibility object. Until then this is one branch a frame.
	return FSlateApplicationBase::IsInitialized()
		&& FSlateApplicationBase::Get().GetAccessibleMessageHandler()->IsActive();
#else
	return false;
#endif
}

void FFolioAccessibilityTree::Collect(const TSharedPtr<SFolioAccessibleElement>& At,
	TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>>& Out)
{
	if (!At.IsValid())
	{
		return;
	}

	if (Rml::Element* Element = At->GetElement())
	{
		Out.Add(Element, At);
	}

	for (int32 Index = 0; Index < At->NumAccessibleChildren(); ++Index)
	{
		Collect(At->GetAccessibleChild(Index), Out);
	}
}

TSharedRef<SFolioAccessibleElement> FFolioAccessibilityTree::Sync(const Folio::FSemanticNode& Node,
	const float ContextToLocal, const TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>>& Existing,
	bool& bOutChanged)
{
	TSharedPtr<SFolioAccessibleElement> Proxy = Existing.FindRef(Node.Element);

	// The role is fixed when the widget is made, because the platform reads its type once and caches
	// it. An element that has become something else is a different thing to announce.
	if (!Proxy.IsValid() || Proxy->GetRole() != Node.Role)
	{
		Proxy = SNew(SFolioAccessibleElement, Node.Role);
		Proxy->OnChanged = OnChanged;
		Proxy->SetAnnouncesOnArrival(bBuiltOnce);
		bOutChanged = true;
	}

	Proxy->Update(Node, ContextToLocal);

	TArray<TSharedRef<SFolioAccessibleElement>> NewChildren;
	NewChildren.Reserve(int32(Node.Children.size()));
	for (const Folio::FSemanticNode& Child : Node.Children)
	{
		NewChildren.Add(Sync(Child, ContextToLocal, Existing, bOutChanged));
	}

	if (!Proxy->HasSameChildren(NewChildren))
	{
		Proxy->SetAccessibleChildren(MoveTemp(NewChildren));
		bOutChanged = true;
	}

	ByElement.Add(Node.Element, Proxy);
	return Proxy.ToSharedRef();
}

bool FFolioAccessibilityTree::Refresh(Rml::ElementDocument* Document, const float ContextToLocal)
{
	if (!Document)
	{
		const bool bHad = Root.IsValid();
		Clear();
		return bHad;
	}

	TMap<Rml::Element*, TSharedPtr<SFolioAccessibleElement>> Existing;
	Collect(Root, Existing);

	ByElement.Reset();

	bool bChanged = false;
	const Folio::FSemanticNode Semantic = Folio::BuildSemanticTree(Document);
	const TSharedRef<SFolioAccessibleElement> NewRoot = Sync(Semantic, ContextToLocal, Existing, bChanged);

	if (Root != NewRoot)
	{
		Root = NewRoot;
		bChanged = true;
	}

	bBuiltOnce = true;
	return bChanged;
}

void FFolioAccessibilityTree::Announce(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}

	// After whatever is being said rather than over it: an announcement is news, and a player halfway
	// through hearing which control they are on still needs the end of that sentence.
	SpeakLine(Text, /*bInterrupt*/ false);

#if WITH_ACCESSIBILITY
	if (FSlateApplicationBase::IsInitialized())
	{
		FSlateApplicationBase::Get().GetAccessibleMessageHandler()->MakeAccessibleAnnouncement(Text);
	}
#endif
}

void FFolioAccessibilityTree::SpeakLine(const FString& Line, const bool bInterrupt)
{
	if (Line.IsEmpty())
	{
		return;
	}

	GLastSpoken = Line;

	UE_LOG(LogFolio, Verbose, TEXT("speaking: %s"), *Line);
	Folio::FSpeech::Say(Line, bInterrupt);
}

const FString& FFolioAccessibilityTree::GetLastSpoken()
{
	return GLastSpoken;
}

void FFolioAccessibilityTree::Clear()
{
	Root.Reset();
	ByElement.Reset();
	LastFocused = nullptr;
	SpokenGroup.Reset();
	EditElement = Rml::ObserverPtr<Rml::Element>();
	EditText.Reset();
	bBuiltOnce = false;
}

void FFolioAccessibilityTree::SyncFocus(Rml::Context* Context)
{
	if (!Root.IsValid() || !Context)
	{
		return;
	}

	Rml::Element* Focused = Context->GetFocusElement();
	if (Focused == LastFocused)
	{
		return;
	}

	LastFocused = Focused;

	// So that reviewing and navigating agree about where the player is. Someone who arrows through a
	// menu and then starts reading should read on from the control they are standing on.
	FFolioReview::FollowFocus(Focused);

	// Up the document from whatever holds focus: RmlUi focuses the element the author gave a tab
	// index to, and the tree keeps the control rather than every div between it and the body.
	for (Rml::Element* At = Focused; At; At = At->GetParentNode())
	{
		const TWeakPtr<SFolioAccessibleElement>* Found = ByElement.Find(At);
		if (!Found)
		{
			continue;
		}

		const TSharedPtr<SFolioAccessibleElement> Proxy = Found->Pin();
		if (!Proxy.IsValid())
		{
			return;
		}

		Speak(Proxy.ToSharedRef());

#if WITH_ACCESSIBILITY
		if (FSlateApplicationBase::IsInitialized())
		{
			FSlateApplicationBase::Get().GetAccessibleMessageHandler()->OnWidgetEventRaised(
				FSlateAccessibleMessageHandler::FSlateWidgetAccessibleEventArgs(Proxy.ToSharedRef(),
					EAccessibleEvent::FocusChange, false, true));
		}
#endif

		return;
	}
}

void FFolioAccessibilityTree::Speak(const TSharedRef<SFolioAccessibleElement>& Proxy)
{
	if (!GScreenReader)
	{
		return;
	}

	FString Line = Folio::Utterance(Proxy->GetNode());
	if (Line.IsEmpty())
	{
		return;
	}

	// The panel is named only on the way into it. Repeating it on every row turns a settings page
	// into "Audio, master volume. Audio, output device. Audio, subtitles."
	if (const FString Group = GroupOf(Proxy.Get()); Group != SpokenGroup)
	{
		SpokenGroup = Group;
		if (!Group.IsEmpty())
		{
			Line = Folio::Spoken::Join({Group, Line});
		}
	}

	SpeakLine(Line, /*bInterrupt*/ true);
}

namespace
{
	/**
	 * The text, caret and selection of a field, if this is one.
	 *
	 * Both of RmlUi's text controls carry the same three, and neither exposes them through the base
	 * class - so this is two casts rather than one, and there is no third case to add.
	 */
	bool ReadEditState(Rml::Element* Element, FString& OutText, int32& OutCaret, int32& OutSelectionEnd,
		bool& bOutPassword)
	{
		if (!Element)
		{
			return false;
		}

		Rml::String Value;
		int Start = 0;
		int End = 0;

		if (auto* Input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(Element))
		{
			// A checkbox and a range are inputs too, and neither has text to echo.
			const Rml::String Type = Element->GetAttribute<Rml::String>("type", "text");
			if (Type != "text" && Type != "password")
			{
				return false;
			}

			Value = Input->GetValue();
			Input->GetSelection(&Start, &End, nullptr);
			bOutPassword = Type == "password";
		}
		else if (auto* Area = rmlui_dynamic_cast<Rml::ElementFormControlTextArea*>(Element))
		{
			Value = Area->GetValue();
			Area->GetSelection(&Start, &End, nullptr);
			bOutPassword = false;
		}
		else
		{
			return false;
		}

		OutText = Folio::ToFString(Value);
		OutCaret = Start;
		OutSelectionEnd = End;
		return true;
	}

	/** How much of two strings is the same from the front, and from the back of what is left. */
	void Difference(const FString& Before, const FString& After, int32& OutPrefix, int32& OutSuffix)
	{
		const int32 Shortest = FMath::Min(Before.Len(), After.Len());

		OutPrefix = 0;
		while (OutPrefix < Shortest && Before[OutPrefix] == After[OutPrefix])
		{
			++OutPrefix;
		}

		OutSuffix = 0;
		while (OutSuffix < Shortest - OutPrefix
			&& Before[Before.Len() - 1 - OutSuffix] == After[After.Len() - 1 - OutSuffix])
		{
			++OutSuffix;
		}
	}

	/** What a password field says instead of its contents, one star per character. */
	FString Masked(const int32 Count)
	{
		return FString::ChrN(FMath::Max(0, Count), TEXT('*'));
	}
}

void FFolioAccessibilityTree::SyncEditing(Rml::Context* Context)
{
	// Only the game's own reader. A platform reader echoes typing through its own text pattern, and
	// doing it here as well would say every letter twice.
	if (!GScreenReader || !Context)
	{
		return;
	}

	Rml::Element* Focused = Context->GetFocusElement();

	FString Text;
	int32 Caret = 0;
	int32 SelectionEnd = 0;
	bool bPassword = false;

	if (!ReadEditState(Focused, Text, Caret, SelectionEnd, bPassword))
	{
		EditElement = Rml::ObserverPtr<Rml::Element>();
		EditText.Reset();
		return;
	}

	// A field that has only just been focused is already being announced by the focus change, which
	// carries its contents. Recorded silently, so the first real keystroke is the first thing said.
	if (EditElement.get() != Focused)
	{
		EditElement = Focused->GetObserverPtr();
		EditText = Text;
		EditCaret = Caret;
		EditSelectionEnd = SelectionEnd;
		return;
	}

	if (Text != EditText)
	{
		int32 Prefix = 0;
		int32 Suffix = 0;
		Difference(EditText, Text, Prefix, Suffix);

		const FString Added = Text.Mid(Prefix, Text.Len() - Suffix - Prefix);
		const FString Removed = EditText.Mid(Prefix, EditText.Len() - Suffix - Prefix);

		if (!Added.IsEmpty())
		{
			SpeakLine(Folio::Spoken::Typed(bPassword ? Masked(Added.Len()) : Added).ToString(), true);
		}
		else if (!Removed.IsEmpty())
		{
			SpeakLine(Folio::Spoken::Deleted(
				Folio::Spoken::Typed(bPassword ? Masked(Removed.Len()) : Removed)).ToString(), true);
		}

		// Said after whatever moved, because an empty field is the result rather than the event. This
		// is the one thing a player cannot infer: a box they have just cleared and a box that was
		// never theirs sound identical otherwise.
		if (Text.IsEmpty())
		{
			SpeakLine(Folio::Spoken::Blank().ToString(), false);
		}
	}
	else if (Caret != EditCaret || SelectionEnd != EditSelectionEnd)
	{
		if (Caret != SelectionEnd)
		{
			const FString Selected = Text.Mid(Caret, SelectionEnd - Caret);
			SpeakLine(Folio::Spoken::Selected(
				Folio::Spoken::Typed(bPassword ? Masked(Selected.Len()) : Selected)).ToString(), true);
		}
		else if (!bPassword)
		{
			// What the caret went past, which is what a reader says when you arrow through a line. Going
			// right, that is the text it left behind; going left, the text it has arrived on.
			const int32 From = FMath::Min(Caret, EditCaret);
			const int32 Length = FMath::Abs(Caret - EditCaret);
			if (const FString Passed = Text.Mid(From, Length); !Passed.IsEmpty())
			{
				SpeakLine(Folio::Spoken::Typed(Passed).ToString(), true);
			}
		}
	}

	EditText = Text;
	EditCaret = Caret;
	EditSelectionEnd = SelectionEnd;
}

void FFolioAccessibilityTree::Describe(TArray<FString>& Out) const
{
	TFunction<void(const TSharedPtr<SFolioAccessibleElement>&, int32)> Walk =
		[&Out, &Walk](const TSharedPtr<SFolioAccessibleElement>& At, const int32 Depth)
	{
		if (!At.IsValid())
		{
			return;
		}

		Out.Add(FString::ChrN(Depth * 2, TEXT(' ')) + At->Describe());

		for (int32 Index = 0; Index < At->NumAccessibleChildren(); ++Index)
		{
			Walk(At->GetAccessibleChild(Index), Depth + 1);
		}
	};

	Walk(Root, 0);
}

void UFolioAccessibilityLibrary::Announce(const FString& Text)
{
	FFolioAccessibilityTree::Announce(Text);
}

void UFolioAccessibilityLibrary::SetScreenReader(const bool bEnabled)
{
	FFolioAccessibilityTree::SetScreenReader(bEnabled);
}

bool UFolioAccessibilityLibrary::IsScreenReaderEnabled()
{
	return FFolioAccessibilityTree::IsScreenReaderEnabled();
}

void UFolioAccessibilityLibrary::SetSpeechRate(const int32 Rate)
{
	Folio::FSpeech::SetRate(Rate);
}

int32 UFolioAccessibilityLibrary::GetSpeechRate()
{
	return Folio::FSpeech::GetRate();
}

void UFolioAccessibilityLibrary::SetSpeechVolume(const int32 Volume)
{
	Folio::FSpeech::SetVolume(Volume);
}

int32 UFolioAccessibilityLibrary::GetSpeechVolume()
{
	return Folio::FSpeech::GetVolume();
}

bool UFolioAccessibilityLibrary::IsSpeechAvailable()
{
	return Folio::FSpeech::IsAvailable();
}

void UFolioAccessibilityLibrary::SetTextScale(const float Scale)
{
	Folio::Presentation::SetTextScale(Scale);
}

float UFolioAccessibilityLibrary::GetTextScale()
{
	return Folio::Presentation::GetTextScale();
}

void UFolioAccessibilityLibrary::SetReducedMotion(const bool bReduce)
{
	Folio::Presentation::SetReducedMotion(bReduce);
}

bool UFolioAccessibilityLibrary::IsReducedMotionOn()
{
	return Folio::Presentation::IsReducedMotionOn();
}

void UFolioAccessibilityLibrary::SetHighContrast(const bool bEnabled)
{
	Folio::Presentation::SetHighContrast(bEnabled);
}

bool UFolioAccessibilityLibrary::IsHighContrastOn()
{
	return Folio::Presentation::IsHighContrastOn();
}

FString UFolioAccessibilityLibrary::ReviewNext()
{
	return FFolioReview::Move(FFolioReview::EMove::Next);
}

FString UFolioAccessibilityLibrary::ReviewPrevious()
{
	return FFolioReview::Move(FFolioReview::EMove::Previous);
}

FString UFolioAccessibilityLibrary::ReviewFirst()
{
	return FFolioReview::Move(FFolioReview::EMove::First);
}

FString UFolioAccessibilityLibrary::ReviewLast()
{
	return FFolioReview::Move(FFolioReview::EMove::Last);
}

FString UFolioAccessibilityLibrary::ReviewRepeat()
{
	return FFolioReview::Move(FFolioReview::EMove::Repeat);
}

FString UFolioAccessibilityLibrary::ReviewAll()
{
	return FFolioReview::ReadAll();
}

void UFolioAccessibilityLibrary::ReviewStop()
{
	FFolioReview::Stop();
}

bool UFolioAccessibilityLibrary::ReviewActivate()
{
	return FFolioReview::Activate();
}

bool UFolioAccessibilityLibrary::ReviewMoveFocusHere()
{
	return FFolioReview::MoveFocusHere();
}

bool UFolioAccessibilityLibrary::IsScreenReaderActive()
{
	if (FFolioAccessibilityTree::IsScreenReaderEnabled())
	{
		return true;
	}

#if WITH_ACCESSIBILITY
	return FSlateApplicationBase::IsInitialized()
		&& FSlateApplicationBase::Get().GetAccessibleMessageHandler()->IsActive();
#else
	return false;
#endif
}

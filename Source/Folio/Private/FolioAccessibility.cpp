#include "FolioAccessibility.h"

#include "FolioAccessibilityLibrary.h"
#include "FolioAccessibilityReview.h"
#include "FolioAccessibilityText.h"
#include "FolioDocumentHost.h"
#include "FolioInsights.h"
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

	TAutoConsoleVariable<float> CVarWalkInterval(
		TEXT("folio.Accessibility.WalkInterval"), 0.1f,
		TEXT("Seconds between structural rebuilds of the screen reader tree. Between them the widgets "
			"still follow their elements. Zero rebuilds on every frame the document changed."),
		ECVF_Default);

	/** Whether the game is reading itself out. A game setting owns this; nothing turns it on by itself. */
	bool GScreenReader = false;

	/** How much the reader says beyond the name. A player's setting; a menu is unbearable at the wrong one. */
	EFolioReaderVerbosity GVerbosity = EFolioReaderVerbosity::Normal;

	/** How much of a field's punctuation is named as it is typed. A separate axis from verbosity. */
	EFolioPunctuation GPunctuation = EFolioPunctuation::Some;

	/** The last thing said, whichever part of the reader said it. What review's repeat repeats. */
	FString GLastSpoken;

	/**
	 * Who is holding the tree open besides a screen reader, and how many times.
	 *
	 * A UI Automation harness driving the menus, or anything else that needs the platform to see the
	 * document. Named rather than counted alone, so a leaked request is a name in a dump.
	 */
	TMap<FName, int32> GTreeRequests;

	/**
	 * A channel name for one element, so a line about it replaces the last line about it.
	 *
	 * The pointer rather than the id, because most elements that announce do not have one and two
	 * documents may reuse the same id.
	 */
	FName ChannelFor(const Rml::Element* Element)
	{
		return FName(*FString::Printf(TEXT("folio.el.%llu"), (uint64)(UPTRINT)Element));
	}

	/** The nearest named group above a control, which is the context a player has just moved into. */
	FString GroupOf(const SFolioAccessibleElement& Proxy)
	{
		for (TSharedPtr<SFolioAccessibleElement> At = Proxy.GetAccessibleParent(); At.IsValid();
			At = At->GetAccessibleParent())
		{
			const Folio::ESemanticRole Role = At->GetRole();
			if ((Role == Folio::ESemanticRole::Group || Role == Folio::ESemanticRole::Region)
				&& !At->GetName().IsEmpty())
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
		case Folio::ESemanticRole::Region: return EAccessibleWidgetType::Layout;
		case Folio::ESemanticRole::Text: return EAccessibleWidgetType::Text;

		// There is no heading in Unreal's list. Text is the closest honest thing: a platform reader
		// says the words and loses the structure, and the game's own reader says "heading" as well.
		case Folio::ESemanticRole::Heading: return EAccessibleWidgetType::Text;
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

		// No table type either, so the shape is lost and the words are not. A cell that carries its
		// column name still reads as "armour, 40" rather than "40".
		case Folio::ESemanticRole::Table: return EAccessibleWidgetType::Layout;
		case Folio::ESemanticRole::Row: return EAccessibleWidgetType::Layout;
		case Folio::ESemanticRole::Cell: return EAccessibleWidgetType::Text;
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

	// The column comes first, because it is what the value means rather than something about it: "armour,
	// 40" is a fact and "40, armour" is a guess followed by a correction.
	if (!Node.Column.empty())
	{
		Parts.Add(Folio::ToFString(Node.Column));
	}

	if (!Node.Name.empty())
	{
		Parts.Add(Folio::ToFString(Node.Name));
	}

	// What kind of thing it is, which someone who already knows the screen does not need on every row.
	if (GVerbosity != EFolioReaderVerbosity::Low)
	{
		if (const FText Role = Folio::Spoken::Role(Node.Role); !Role.IsEmpty())
		{
			Parts.Add(Role.ToString());
		}
	}

	// How far through the set, which is the one thing someone who cannot see a list has no other way of
	// learning - including that there is more of it below.
	if (Node.SetSize > 1 && GVerbosity != EFolioReaderVerbosity::Low)
	{
		Parts.Add(Folio::Spoken::Position(Node.Position, Node.SetSize).ToString());
	}

	if (Node.Checkable)
	{
		Parts.Add(Folio::Spoken::Checked(Node.Checked).ToString());
	}

	if (!Node.Value.empty() && !Folio::RoleSaysItsOwnValue(Node.Role))
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

	// The consequence, which is worth hearing once and unbearable on every row of a settings page.
	if (GVerbosity == EFolioReaderVerbosity::High && !Node.Description.empty())
	{
		Parts.Add(Folio::ToFString(Node.Description));
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
		//
		// On its own channel, so a number that keeps changing leaves one line waiting rather than one
		// per frame. That is what a player holding a key on a slider produces.
		if (Node.Announces && bNews && !NewName.IsEmpty())
		{
			FFolioAccessibilityTree::SpeakLine(NewName,
				Node.AnnouncesAssertively ? Folio::ESpeechPriority::Assertive
					: Folio::ESpeechPriority::Polite,
				ChannelFor(Element.get()));
		}
	}

	bUpdatedOnce = true;
}

bool SFolioAccessibleElement::SyncGeometry(const float InContextToLocal)
{
	Rml::Element* Target = Element.get();
	if (!Target)
	{
		return false;
	}

	ContextToLocal = InContextToLocal;
	Node.Offset = Target->GetAbsoluteOffset(Rml::BoxArea::Border);
	Node.Size = Target->GetBox().GetSize(Rml::BoxArea::Border);
	return true;
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

	if (!ValueText.IsEmpty() && !Folio::RoleSaysItsOwnValue(Role))
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
			SpeakLine(Folio::Spoken::ReaderOn().ToString(), Folio::ESpeechPriority::Assertive);
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

void FFolioAccessibilityTree::AddRequest(const FName Reason)
{
	++GTreeRequests.FindOrAdd(Reason);
}

void FFolioAccessibilityTree::RemoveRequest(const FName Reason)
{
	int32* Count = GTreeRequests.Find(Reason);
	if (!Count)
	{
		UE_LOG(LogFolio, Warning,
			TEXT("released an accessibility tree request for '%s' that was never taken"),
			*Reason.ToString());

		return;
	}

	if (--(*Count) <= 0)
	{
		GTreeRequests.Remove(Reason);
	}
}

void FFolioAccessibilityTree::DescribeRequests(TArray<FString>& Out)
{
	for (const TPair<FName, int32>& Request : GTreeRequests)
	{
		Out.Add(FString::Printf(TEXT("%s x%d"), *Request.Key.ToString(), Request.Value));
	}
}

bool FFolioAccessibilityTree::IsWanted()
{
	if (GScreenReader || !GTreeRequests.IsEmpty()
		|| CVarForceAccessibility.GetValueOnGameThread() != 0)
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

bool FFolioAccessibilityTree::Refresh(Rml::ElementDocument* Document, const float ContextToLocal,
	const bool bForce)
{
	if (!Document)
	{
		const bool bHad = Root.IsValid();
		Clear();
		return bHad;
	}

	// A document that changed did not necessarily change what it means. A HUD animating a bar moves
	// every frame and says the same thing all day, so between rebuilds the proxies are only moved to
	// where their elements now are - which is a box read per node, with no walk and no strings.
	const double Now = FPlatformTime::Seconds();
	const double Interval = double(FMath::Max(0.0f, CVarWalkInterval.GetValueOnGameThread()));

	if (!bForce && !bStale && Root.IsValid() && Now - LastRebuild < Interval)
	{
		bool bAllAlive = true;
		for (const TPair<Rml::Element*, TWeakPtr<SFolioAccessibleElement>>& Pair : ByElement)
		{
			const TSharedPtr<SFolioAccessibleElement> Proxy = Pair.Value.Pin();
			if (Proxy.IsValid() && !Proxy->SyncGeometry(ContextToLocal))
			{
				// An element the document has freed under us. Nothing here is safe to keep.
				bAllAlive = false;
				break;
			}
		}

		if (bAllAlive)
		{
			return false;
		}
	}

	LastRebuild = Now;
	bStale = false;

	// Only the walk, not the geometry pass above it. What `stat Folio` should show on a screen nobody
	// is reading is nothing at all.
	SCOPE_CYCLE_COUNTER(STAT_FolioAccessibility);

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
	SpeakLine(Text, Folio::ESpeechPriority::Polite);

#if WITH_ACCESSIBILITY
	if (FSlateApplicationBase::IsInitialized())
	{
		FSlateApplicationBase::Get().GetAccessibleMessageHandler()->MakeAccessibleAnnouncement(Text);
	}
#endif
}

void FFolioAccessibilityTree::SpeakLine(const FString& Line, const Folio::ESpeechPriority Priority,
	const FName Channel)
{
	if (Line.IsEmpty())
	{
		return;
	}

	GLastSpoken = Line;

	UE_LOG(LogFolio, Verbose, TEXT("speaking: %s"), *Line);
	Folio::FSpeech::Say(Line, Priority, Channel);
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

	// Focus is somewhere the tree has never heard of, which means it arrived between rebuilds - a
	// dialog that opened this frame. The next update walks rather than waiting its turn, so the player
	// hears where they are one frame later instead of a tenth of a second later.
	if (Focused)
	{
		bStale = true;

		// Forgotten as well as marked, or the next pass sees the same element and says nothing.
		LastFocused = nullptr;
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
	if (const FString Group = GVerbosity == EFolioReaderVerbosity::Low ? FString() : GroupOf(Proxy.Get());
		Group != SpokenGroup)
	{
		SpokenGroup = Group;
		if (!Group.IsEmpty())
		{
			Line = Folio::Spoken::Join({Group, Line});
		}
	}

	SpeakLine(Line, Folio::ESpeechPriority::Assertive);
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

namespace
{
	/** The focused text field of whichever document is in front, with its contents and caret. */
	bool FocusedEdit(FString& OutText, int32& OutCaret, bool& bOutPassword)
	{
		const TArray<FFolioDocumentHost*>& Hosts = FFolioDocumentHost::GetLiveHosts();
		for (int32 Index = Hosts.Num() - 1; Index >= 0; --Index)
		{
			Rml::Context* Context = Hosts[Index]->GetContext();
			if (!Context)
			{
				continue;
			}

			int32 End = 0;
			if (ReadEditState(Context->GetFocusElement(), OutText, OutCaret, End, bOutPassword))
			{
				return true;
			}
		}

		return false;
	}

	/** The run of non-space characters the caret is standing in, or the one it has just passed. */
	void WordAround(const FString& Text, const int32 Caret, int32& OutStart, int32& OutEnd)
	{
		const auto IsWord = [&Text](const int32 At)
		{
			return Text.IsValidIndex(At) && !FChar::IsWhitespace(Text[At]);
		};

		// A caret sitting just past the end of a word is in that word as far as a person is concerned,
		// which is where it lands after typing one.
		int32 At = IsWord(Caret) ? Caret : Caret - 1;

		OutStart = At;
		OutEnd = At;

		while (IsWord(OutStart - 1))
		{
			--OutStart;
		}

		while (IsWord(OutEnd))
		{
			++OutEnd;
		}

		OutStart = FMath::Max(0, OutStart);
	}

	void LineAround(const FString& Text, const int32 Caret, int32& OutStart, int32& OutEnd)
	{
		OutStart = FMath::Clamp(Caret, 0, Text.Len());
		while (OutStart > 0 && Text[OutStart - 1] != TEXT('\n'))
		{
			--OutStart;
		}

		OutEnd = FMath::Clamp(Caret, 0, Text.Len());
		while (OutEnd < Text.Len() && Text[OutEnd] != TEXT('\n'))
		{
			++OutEnd;
		}
	}
}

FString FFolioAccessibilityTree::ReadEditWord()
{
	FString Text;
	int32 Caret = 0;
	bool bPassword = false;

	if (!FocusedEdit(Text, Caret, bPassword))
	{
		return FString();
	}

	int32 Start = 0;
	int32 End = 0;
	WordAround(Text, Caret, Start, End);

	const FString Word = End > Start ? Text.Mid(Start, End - Start) : FString();
	const FString Line = Word.IsEmpty()
		? Folio::Spoken::NoWord().ToString()
		: Folio::Spoken::Typed(bPassword ? Masked(Word.Len()) : Word, GPunctuation).ToString();

	SpeakLine(Line, Folio::ESpeechPriority::Assertive);
	return Line;
}

FString FFolioAccessibilityTree::ReadEditLine()
{
	FString Text;
	int32 Caret = 0;
	bool bPassword = false;

	if (!FocusedEdit(Text, Caret, bPassword))
	{
		return FString();
	}

	int32 Start = 0;
	int32 End = 0;
	LineAround(Text, Caret, Start, End);

	const FString Content = End > Start ? Text.Mid(Start, End - Start) : FString();
	const FString Line = Content.IsEmpty()
		? Folio::Spoken::Blank().ToString()
		: Folio::Spoken::Typed(bPassword ? Masked(Content.Len()) : Content, GPunctuation).ToString();

	SpeakLine(Line, Folio::ESpeechPriority::Assertive);
	return Line;
}

FString FFolioAccessibilityTree::SpellEditWord()
{
	FString Text;
	int32 Caret = 0;
	bool bPassword = false;

	if (!FocusedEdit(Text, Caret, bPassword))
	{
		return FString();
	}

	int32 Start = 0;
	int32 End = 0;
	WordAround(Text, Caret, Start, End);

	const FString Word = End > Start ? Text.Mid(Start, End - Start) : FString();
	const FString Line = Word.IsEmpty()
		? Folio::Spoken::NoWord().ToString()
		: Folio::Spoken::Spelled(bPassword ? Masked(Word.Len()) : Word).ToString();

	SpeakLine(Line, Folio::ESpeechPriority::Assertive);
	return Line;
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
			SpeakLine(Folio::Spoken::Typed(bPassword ? Masked(Added.Len()) : Added, GPunctuation).ToString(), Folio::ESpeechPriority::Assertive);
		}
		else if (!Removed.IsEmpty())
		{
			SpeakLine(Folio::Spoken::Deleted(
				Folio::Spoken::Typed(bPassword ? Masked(Removed.Len()) : Removed, GPunctuation)).ToString(), Folio::ESpeechPriority::Assertive);
		}

		// Said after whatever moved, because an empty field is the result rather than the event. This
		// is the one thing a player cannot infer: a box they have just cleared and a box that was
		// never theirs sound identical otherwise.
		if (Text.IsEmpty())
		{
			SpeakLine(Folio::Spoken::Blank().ToString(), Folio::ESpeechPriority::Polite);
		}
	}
	else if (Caret != EditCaret || SelectionEnd != EditSelectionEnd)
	{
		if (Caret != SelectionEnd)
		{
			const FString Selected = Text.Mid(Caret, SelectionEnd - Caret);
			SpeakLine(Folio::Spoken::Selected(
				Folio::Spoken::Typed(bPassword ? Masked(Selected.Len()) : Selected, GPunctuation)).ToString(), Folio::ESpeechPriority::Assertive);
		}
		else if (!bPassword)
		{
			// What the caret went past, which is what a reader says when you arrow through a line. Going
			// right, that is the text it left behind; going left, the text it has arrived on.
			const int32 From = FMath::Min(Caret, EditCaret);
			const int32 Length = FMath::Abs(Caret - EditCaret);
			if (const FString Passed = Text.Mid(From, Length); !Passed.IsEmpty())
			{
				SpeakLine(Folio::Spoken::Typed(Passed, GPunctuation).ToString(), Folio::ESpeechPriority::Assertive);
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

void UFolioAccessibilityLibrary::SetTextSpeed(const float Multiplier)
{
	Folio::Presentation::SetTextSpeed(Multiplier);
}

float UFolioAccessibilityLibrary::GetTextSpeed()
{
	return Folio::Presentation::GetTextSpeed();
}

void UFolioAccessibilityLibrary::SetInstantText(const bool bInstant)
{
	Folio::Presentation::SetInstantText(bInstant);
}

bool UFolioAccessibilityLibrary::IsInstantTextOn()
{
	return Folio::Presentation::IsInstantTextOn();
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

FString UFolioAccessibilityLibrary::ReviewNextHeading()
{
	return FFolioReview::Move(FFolioReview::EMove::NextHeading);
}

FString UFolioAccessibilityLibrary::ReviewPreviousHeading()
{
	return FFolioReview::Move(FFolioReview::EMove::PreviousHeading);
}

FFolioDocumentTreeRequest::FFolioDocumentTreeRequest(const FName InReason)
	: Reason(InReason)
{
	FFolioAccessibilityTree::AddRequest(Reason);
}

FFolioDocumentTreeRequest::~FFolioDocumentTreeRequest()
{
	FFolioAccessibilityTree::RemoveRequest(Reason);
}

TArray<FString> UFolioAccessibilityLibrary::DescribeScreen()
{
	TArray<FString> Lines;
	FFolioReview::Lines(Lines);
	return Lines;
}

FString UFolioAccessibilityLibrary::GetFocusedControl()
{
	return FFolioReview::Focused();
}

void UFolioAccessibilityLibrary::AddDocumentTreeRequest(const FName Reason)
{
	FFolioAccessibilityTree::AddRequest(Reason);
}

void UFolioAccessibilityLibrary::RemoveDocumentTreeRequest(const FName Reason)
{
	FFolioAccessibilityTree::RemoveRequest(Reason);
}

bool UFolioAccessibilityLibrary::IsDocumentTreeBuilt()
{
	return FFolioAccessibilityTree::IsWanted();
}

void UFolioAccessibilityLibrary::SetReaderVerbosity(const EFolioReaderVerbosity Verbosity)
{
	GVerbosity = Verbosity;
}

EFolioReaderVerbosity UFolioAccessibilityLibrary::GetReaderVerbosity()
{
	return GVerbosity;
}

void UFolioAccessibilityLibrary::SetPunctuation(const EFolioPunctuation Level)
{
	GPunctuation = Level;
}

EFolioPunctuation UFolioAccessibilityLibrary::GetPunctuation()
{
	return GPunctuation;
}

FString UFolioAccessibilityLibrary::ReadEditWord()
{
	return FFolioAccessibilityTree::ReadEditWord();
}

FString UFolioAccessibilityLibrary::ReadEditLine()
{
	return FFolioAccessibilityTree::ReadEditLine();
}

FString UFolioAccessibilityLibrary::SpellEditWord()
{
	return FFolioAccessibilityTree::SpellEditWord();
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

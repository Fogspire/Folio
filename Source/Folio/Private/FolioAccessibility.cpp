#include "FolioAccessibility.h"

#include "FolioAccessibilityLibrary.h"
#include "FolioModule.h"
#include "FolioRmlUi.h"
#include "FolioSpeech.h"

#include "HAL/IConsoleManager.h"
#include "Layout/ArrangedChildren.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

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

	/**
	 * What a control is called out loud.
	 *
	 * A role word rather than a role name: "check box" is what every screen reader says and what a
	 * player expects to hear, where "checkbox" reads as a tag name to anyone who has used one.
	 */
	const TCHAR* SpokenRole(const Folio::ESemanticRole Role)
	{
		switch (Role)
		{
		case Folio::ESemanticRole::Image: return TEXT("image");
		case Folio::ESemanticRole::Button: return TEXT("button");
		case Folio::ESemanticRole::Link: return TEXT("link");
		case Folio::ESemanticRole::CheckBox: return TEXT("check box");
		case Folio::ESemanticRole::Radio: return TEXT("radio button");
		case Folio::ESemanticRole::ComboBox: return TEXT("combo box");
		case Folio::ESemanticRole::Slider: return TEXT("slider");
		case Folio::ESemanticRole::ProgressBar: return TEXT("progress");
		case Folio::ESemanticRole::TextEdit: return TEXT("edit");
		case Folio::ESemanticRole::List: return TEXT("list");
		case Folio::ESemanticRole::ListItem: return TEXT("item");

		// Text says itself, and a group is a place rather than a thing to hear the kind of.
		default: return nullptr;
		}
	}

	/** Name, then what it is, then what state it is in - which is the order every reader uses. */
	FString Utterance(const SFolioAccessibleElement& Proxy)
	{
		TArray<FString> Parts;

		if (!Proxy.GetName().IsEmpty())
		{
			Parts.Add(Proxy.GetName().ToString());
		}

		if (const TCHAR* Role = SpokenRole(Proxy.GetRole()))
		{
			Parts.Add(Role);
		}

		const Folio::FSemanticNode& Node = Proxy.GetNode();

		if (Node.Checkable)
		{
			Parts.Add(Node.Checked ? TEXT("checked") : TEXT("not checked"));
		}

		if (!Proxy.GetValueText().IsEmpty() && Proxy.GetRole() != Folio::ESemanticRole::Text)
		{
			Parts.Add(Proxy.GetValueText());
		}

		// The word a reader uses for a control that is present and cannot be used. Said last, because
		// it qualifies everything before it.
		if (Node.Disabled)
		{
			Parts.Add(TEXT("unavailable"));
		}

		return FString::Join(Parts, TEXT(", "));
	}

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
			Folio::FSpeech::Say(TEXT("Screen reader on"), true);
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
	Folio::FSpeech::Say(Text, /*bInterrupt*/ false);

#if WITH_ACCESSIBILITY
	if (FSlateApplicationBase::IsInitialized())
	{
		FSlateApplicationBase::Get().GetAccessibleMessageHandler()->MakeAccessibleAnnouncement(Text);
	}
#endif
}

void FFolioAccessibilityTree::Clear()
{
	Root.Reset();
	ByElement.Reset();
	LastFocused = nullptr;
	SpokenGroup.Reset();
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

	FString Line = Utterance(Proxy.Get());
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
			Line = Group + TEXT(", ") + Line;
		}
	}

	UE_LOG(LogFolio, Verbose, TEXT("speaking: %s"), *Line);
	Folio::FSpeech::Say(Line, /*bInterrupt*/ true);
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

#include "FolioWidget.h"

#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "FolioCore/FolioTextElement.h"
#include "FolioTextures.h"
#include "SFolioCanvas.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "Folio"

TSharedRef<SWidget> UFolioWidget::RebuildWidget()
{
	// A designer preview has no RmlUi and must not try to build one: the module reports its own
	// failure, and a null widget here keeps the palette usable either way.
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		return SNullWidget::NullWidget;
	}

	// Built with no document on purpose. A document resolves its bindings as it loads, so every model
	// has to exist first; passing the path here would load it before ApplyBindings could run and every
	// binding would render as literal braces.
	Canvas = SNew(SFolioCanvas)
		.DesiredSize(DesiredSize)
		.Interactive(bInteractive)
		.ScaleFollow(ScaleFollow)
		.Scale(Scale)
		.MinScale(MinScale)
		.MaxScale(MaxScale)
		.Supersample(Supersample);

	// Once a canvas exists there is something to filter against. Dropped in ReleaseSlateResources,
	// which is also where the canvas goes.
	if (!RevealHandle.IsValid() && FFolioModule::IsAvailable())
	{
		RevealHandle = FFolioModule::Get().OnTextRevealed().AddUObject(this, &UFolioWidget::HandleTextRevealed);
	}

	Canvas->OnBack.BindWeakLambda(this, [this] { OnBack.Broadcast(); });
	Canvas->OnUnclaimedKey.BindWeakLambda(this, [this](const FKey Key) { OnUnclaimedKey.Broadcast(Key); });

	for (const FKey& Key : ClaimedKeys)
	{
		Canvas->ClaimKey(Key);
	}

	ApplyBindings();
	Canvas->LoadDocument(DocumentPath);

	return Canvas.ToSharedRef();
}

void UFolioWidget::ApplyBindings()
{
	if (!Canvas.IsValid())
	{
		return;
	}

	for (const FFolioViewModelBinding& Binding : Bindings)
	{
		if (Binding.Source && !Canvas->HasObjectModel(Binding.ModelName))
		{
			Canvas->BindObjectModel(Binding.ModelName, Binding.Source, Binding.bPollEveryFrame);
		}
	}
}

void UFolioWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	/*
	 * UWidget pushes its own Visibility onto the canvas here, which puts hit testing back on a document
	 * that must not take the mouse. The symptom is the game losing the mouse for good the first time the
	 * viewport's capture goes: a readout that answers a hit test swallows the click that would give the
	 * capture back, so alt-tabbing away is enough to end mouse look until the level is restarted.
	 *
	 * Only the visible case, so a document hidden or collapsed by whatever hosts it stays that way.
	 */
	if (Canvas.IsValid() && !bInteractive && Canvas->GetVisibility() == EVisibility::Visible)
	{
		Canvas->SetVisibility(EVisibility::HitTestInvisible);
	}
}

void UFolioWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	if (RevealHandle.IsValid() && FFolioModule::IsAvailable())
	{
		FFolioModule::Get().OnTextRevealed().Remove(RevealHandle);
	}
	RevealHandle.Reset();

	Canvas.Reset();

	for (const FName Name : OwnedTextures)
	{
		Folio::UnregisterTexture(Name);
	}

	OwnedTextures.Reset();
}

void UFolioWidget::LoadDocument(const FString& InDocumentPath)
{
	DocumentPath = InDocumentPath;

	if (Canvas.IsValid())
	{
		Canvas->LoadDocument(InDocumentPath);
	}
}

bool UFolioWidget::BindViewModel(const FString& ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (ModelName.IsEmpty() || !Source)
	{
		return false;
	}

	// Recorded rather than only applied, so a rebuild - which a designer preview, a re-add to the
	// viewport or a hot reload all cause - puts the same models back.
	const int32 Existing = Bindings.IndexOfByPredicate(
		[&ModelName](const FFolioViewModelBinding& Binding) { return Binding.ModelName.Equals(ModelName, ESearchCase::CaseSensitive); });

	FFolioViewModelBinding Binding;
	Binding.ModelName = ModelName;
	Binding.Source = Source;
	Binding.bPollEveryFrame = bPollEveryFrame;

	if (Existing != INDEX_NONE)
	{
		Bindings[Existing] = MoveTemp(Binding);
	}
	else
	{
		Bindings.Add(MoveTemp(Binding));
	}

	if (!Canvas.IsValid())
	{
		// The widget has not been built yet, which is the ordinary case from a construction script.
		// RebuildWidget applies this before it loads the document.
		return true;
	}

	const bool bDocumentAlreadyUp = Canvas->GetDocument() != nullptr;
	ApplyBindings();

	if (bDocumentAlreadyUp)
	{
		// The document resolved its bindings without this model, and RmlUi gives no way to attach one
		// afterwards. Rebuilding is what makes it visible, and doing it here rather than reporting it
		// means the call does what it says whenever it is made.
		UE_LOG(LogFolio, Log,
			TEXT("BindViewModel('%s') arrived after '%s' had loaded, so the document is being rebuilt. ")
			TEXT("Bind before the document loads to avoid it."),
			*ModelName, *DocumentPath);

		Canvas->ReloadDocument();
	}

	return true;
}

bool UFolioWidget::SetElementProperty(const FString& ElementId, const FString& Property, const FString& Value)
{
	if (!Canvas.IsValid())
	{
		return false;
	}

	Rml::ElementDocument* Document = Canvas->GetDocument();
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(Folio::ToRmlString(ElementId));
	if (!Element)
	{
		return false;
	}

	Element->SetProperty(Folio::ToRmlString(Property), Folio::ToRmlString(Value));
	Canvas->MarkDirty(TEXT("element property"));

	return true;
}

Rml::Element* UFolioWidget::ResolveElement(const FString& ElementId) const
{
	if (!Canvas.IsValid())
	{
		return nullptr;
	}

	Rml::ElementDocument* Document = Canvas->GetDocument();
	return Document ? Document->GetElementById(Folio::ToRmlString(ElementId)) : nullptr;
}

bool UFolioWidget::SetElementMarkup(const FString& ElementId, const FString& Markup)
{
	Folio::Text::FElementRichText* Text = Folio::Text::AsRichText(ResolveElement(ElementId));
	if (!Text)
	{
		return false;
	}

	Text->SetMarkup(Folio::ToRmlString(Markup));

	// Play rather than leaving it to the next update: a line handed over while the previous one was
	// still typing would otherwise carry on from where that one had got to.
	Text->Play();

	Canvas->MarkDirty(TEXT("text markup"));
	return true;
}

bool UFolioWidget::SkipElementReveal(const FString& ElementId)
{
	Folio::Text::FElementRichText* Text = Folio::Text::AsRichText(ResolveElement(ElementId));
	if (!Text)
	{
		return false;
	}

	Text->Skip();
	Canvas->MarkDirty(TEXT("text skip"));
	return true;
}

bool UFolioWidget::IsElementRevealing(const FString& ElementId) const
{
	const Folio::Text::FElementRichText* Text = Folio::Text::AsRichText(ResolveElement(ElementId));
	return Text != nullptr && Text->IsRevealing();
}

float UFolioWidget::GetElementRevealDuration(const FString& ElementId) const
{
	const Folio::Text::FElementRichText* Text = Folio::Text::AsRichText(ResolveElement(ElementId));
	return Text ? Text->GetDuration() : 0.f;
}

void UFolioWidget::HandleTextRevealed(Rml::Element* Element)
{
	// Every widget in the process hears every line, so the one that owns this element is the only
	// one that reports it.
	if (!Element || !Canvas.IsValid() || Element->GetOwnerDocument() != Canvas->GetDocument())
	{
		return;
	}

	OnTextRevealed.Broadcast(Folio::ToFString(Element->GetId()));
}

void UFolioWidget::RegisterTexture(const FName Name, UTexture* Texture)
{
	if (Name.IsNone())
	{
		return;
	}

	Folio::RegisterTexture(Name, Texture);
	OwnedTextures.Add(Name);
}

void UFolioWidget::UnregisterTexture(const FName Name)
{
	Folio::UnregisterTexture(Name);
	OwnedTextures.Remove(Name);
}

void UFolioWidget::ApplyScale()
{
	if (Canvas.IsValid())
	{
		Canvas->SetScale(ScaleFollow, Scale, MinScale, MaxScale);
		Canvas->SetSupersample(Supersample);
	}
}

void UFolioWidget::SetInteractive(const bool bInInteractive)
{
	bInteractive = bInInteractive;

	if (Canvas.IsValid())
	{
		Canvas->SetInteractive(bInInteractive);
	}
}

void UFolioWidget::ClaimKey(const FKey Key)
{
	ClaimedKeys.AddUnique(Key);

	if (Canvas.IsValid())
	{
		Canvas->ClaimKey(Key);
	}
}

void UFolioWidget::ReleaseKey(const FKey Key)
{
	ClaimedKeys.Remove(Key);

	if (Canvas.IsValid())
	{
		Canvas->ReleaseKey(Key);
	}
}

bool UFolioWidget::ScrollElement(const FString& ElementId, const float Fraction)
{
	Rml::ElementDocument* Document = Canvas.IsValid() ? Canvas->GetDocument() : nullptr;
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(Folio::ToRmlString(ElementId));
	if (!Element)
	{
		return false;
	}

	const float Range = Element->GetScrollHeight() - Element->GetClientHeight();
	Element->SetScrollTop(FMath::Clamp(Fraction, 0.0f, 1.0f) * FMath::Max(Range, 0.0f));

	return true;
}

bool UFolioWidget::FocusElement(const FString& ElementId, const bool bVisible)
{
	if (!Canvas.IsValid())
	{
		return false;
	}

	Rml::ElementDocument* Document = Canvas->GetDocument();
	if (!Document)
	{
		return false;
	}

	Rml::Element* Element = Document->GetElementById(Folio::ToRmlString(ElementId));
	return Element && Element->Focus(bVisible);
}

void UFolioWidget::TakeFocus()
{
	if (Canvas.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocus(Canvas, EFocusCause::SetDirectly);
	}
}

void UFolioWidget::SetKeepFocus(const bool bKeepFocus)
{
	if (Canvas.IsValid())
	{
		Canvas->SetKeepFocus(bKeepFocus);
	}
}

#if WITH_EDITOR
const FText UFolioWidget::GetPaletteCategory()
{
	return LOCTEXT("FolioPaletteCategory", "Folio");
}
#endif

#undef LOCTEXT_NAMESPACE

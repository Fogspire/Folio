#include "FolioWidget.h"

#include "FolioModule.h"
#include "SFolioCanvas.h"

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
		.DesiredSize(DesiredSize);

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
		if (Binding.Source && !Canvas->HasObjectModel(Binding.ModelName.ToString()))
		{
			Canvas->BindObjectModel(Binding.ModelName.ToString(), Binding.Source, Binding.bPollEveryFrame);
		}
	}
}

void UFolioWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Canvas.Reset();
}

void UFolioWidget::LoadDocument(const FString& InDocumentPath)
{
	DocumentPath = InDocumentPath;

	if (Canvas.IsValid())
	{
		Canvas->LoadDocument(InDocumentPath);
	}
}

bool UFolioWidget::BindViewModel(const FName ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (ModelName.IsNone() || !Source)
	{
		return false;
	}

	// Recorded rather than only applied, so a rebuild - which a designer preview, a re-add to the
	// viewport or a hot reload all cause - puts the same models back.
	const int32 Existing = Bindings.IndexOfByPredicate(
		[ModelName](const FFolioViewModelBinding& Binding) { return Binding.ModelName == ModelName; });

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
			*ModelName.ToString(), *DocumentPath);

		Canvas->ReloadDocument();
	}

	return true;
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

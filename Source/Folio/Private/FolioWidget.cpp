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

	Canvas = SNew(SFolioCanvas)
		.DocumentPath(DocumentPath)
		.DesiredSize(DesiredSize);

	return Canvas.ToSharedRef();
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
	if (!Canvas.IsValid())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("BindViewModel was called before the widget was built; bind it after construction"));
		return false;
	}

	return Canvas->BindObjectModel(ModelName.ToString(), Source, bPollEveryFrame);
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

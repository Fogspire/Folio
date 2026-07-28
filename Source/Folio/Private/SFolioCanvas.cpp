#include "SFolioCanvas.h"

#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"

void SFolioCanvas::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	PendingDocumentPath = InArgs._DocumentPath;

	SetCanTick(true);

	if (!FFolioModule::Get().IsRmlInitialised())
	{
		return;
	}

	// A context needs a size before it will lay anything out, and the real one only arrives
	// with the first geometry. Starting at the requested desired size means a document loaded
	// now is laid out sensibly rather than into a zero-sized viewport.
	ContextSize = FIntPoint(FMath::RoundToInt(DesiredSize.X), FMath::RoundToInt(DesiredSize.Y));

	static int32 ContextCounter = 0;
	const Rml::String Name = Folio::ToRmlString(FString::Printf(TEXT("folio_%d"), ContextCounter++));

	Context = Rml::CreateContext(Name, Rml::Vector2i(ContextSize.X, ContextSize.Y));
	if (!Context)
	{
		UE_LOG(LogFolio, Error, TEXT("could not create an RmlUi context"));
		return;
	}

	if (!PendingDocumentPath.IsEmpty())
	{
		LoadDocument(PendingDocumentPath);
	}
}

SFolioCanvas::~SFolioCanvas()
{
	DestroyContext();
}

void SFolioCanvas::DestroyContext()
{
	if (!Context)
	{
		return;
	}

	// Closing a document is deferred until the next Update, so the context has to outlive the
	// close. Removing the context destroys everything in it outright, which is what we want.
	Document = nullptr;
	Rml::RemoveContext(Context->GetName());
	Context = nullptr;
}

void SFolioCanvas::LoadDocument(const FString& RelativePath)
{
	if (!Context)
	{
		return;
	}

	if (Document)
	{
		Document->Close();
		// Close is deferred to the next update; without this the old document is still alive
		// when the new one loads and both would render.
		Context->Update();
		Document = nullptr;
	}

	PendingDocumentPath = RelativePath;
	if (RelativePath.IsEmpty())
	{
		return;
	}

	Document = Context->LoadDocument(Folio::ToRmlString(RelativePath));
	if (!Document)
	{
		UE_LOG(LogFolio, Error, TEXT("could not load document '%s'"), *RelativePath);
		return;
	}

	Document->Show();
	UE_LOG(LogFolio, Log, TEXT("loaded '%s'"), *RelativePath);
}

void SFolioCanvas::UpdateContextSize(const FGeometry& AllottedGeometry)
{
	const FVector2f Local = AllottedGeometry.GetLocalSize();
	const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(Local.X)), FMath::Max(1, FMath::RoundToInt(Local.Y)));

	if (Size == ContextSize || !Context)
	{
		return;
	}

	ContextSize = Size;
	Context->SetDimensions(Rml::Vector2i(Size.X, Size.Y));
}

void SFolioCanvas::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!Context)
	{
		return;
	}

	UpdateContextSize(AllottedGeometry);

	// Slate's layout scale is Unreal's UI scaling; RmlUi expresses the same thing as a
	// density independent pixel ratio, which is what dp units resolve against.
	Context->SetDensityIndependentPixelRatio(AllottedGeometry.Scale);

	Context->Update();
}

FVector2D SFolioCanvas::ComputeDesiredSize(float) const
{
	return DesiredSize;
}

int32 SFolioCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	if (!Context)
	{
		return LayerId;
	}

	FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface();
	if (!Renderer)
	{
		return LayerId;
	}

	Renderer->BeginFrame(OutDrawElements, AllottedGeometry, LayerId);
	Context->Render();
	Renderer->EndFrame();

	return FMath::Max(LayerId, Renderer->GetMaxLayerId());
}

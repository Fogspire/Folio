#include "SFolioCanvas.h"

#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioInput.h"
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

	// A widget can outlive the module. Once Rml::Shutdown has run, every context is already
	// destroyed and its name is no longer in RmlUi's map, so looking one up walks freed
	// memory - which surfaces as a crash inside robin_hood's hash table on editor close.
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		Context = nullptr;
		Document = nullptr;
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

	Renderer->BeginFrame(OutDrawElements, AllottedGeometry, LayerId, ContextSize);
	Context->Render();
	Renderer->EndFrame();

	return FMath::Max(LayerId, Renderer->GetMaxLayerId());
}

//
// Input
//

FVector2f SFolioCanvas::ToContextSpace(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	// The context is sized in whole pixels from the widget's local size, so local space is
	// already context space. Slate's layout scale is carried separately as the dp ratio.
	return FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
}

FReply SFolioCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Context)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = ToContextSpace(MyGeometry, MouseEvent);
	Context->ProcessMouseMove(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y),
		Folio::ToRmlModifiers(MouseEvent.GetModifierKeys()));

	return FReply::Unhandled();
}

FReply SFolioCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const int32 Button = Folio::ToRmlMouseButton(MouseEvent.GetEffectingButton());
	if (!Context || Button < 0)
	{
		return FReply::Unhandled();
	}

	// RmlUi resolves a click against the element under the cursor, and it only knows where
	// that is from the last move. A click without a preceding move over the widget - which is
	// what a synthetic or teleported cursor produces - would otherwise hit whatever was last
	// hovered.
	const FVector2f Local = ToContextSpace(MyGeometry, MouseEvent);
	const int32 Modifiers = Folio::ToRmlModifiers(MouseEvent.GetModifierKeys());
	Context->ProcessMouseMove(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y), Modifiers);
	Context->ProcessMouseButtonDown(Button, Modifiers);

	// Capturing keeps the release paired with the press even if the cursor leaves, and taking
	// keyboard focus is what lets a text field receive characters.
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SFolioCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const int32 Button = Folio::ToRmlMouseButton(MouseEvent.GetEffectingButton());
	if (!Context || Button < 0)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = ToContextSpace(MyGeometry, MouseEvent);
	const int32 Modifiers = Folio::ToRmlModifiers(MouseEvent.GetModifierKeys());
	Context->ProcessMouseMove(FMath::RoundToInt(Local.X), FMath::RoundToInt(Local.Y), Modifiers);
	Context->ProcessMouseButtonUp(Button, Modifiers);

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SFolioCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Context)
	{
		return FReply::Unhandled();
	}

	// RmlUi scrolls by lines with positive meaning down, which is the opposite sign to Slate.
	const bool bHandled = !Context->ProcessMouseWheel(-MouseEvent.GetWheelDelta(),
		Folio::ToRmlModifiers(MouseEvent.GetModifierKeys()));

	return bHandled ? FReply::Handled() : FReply::Unhandled();
}

void SFolioCanvas::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (Context)
	{
		// Without this the last hovered element keeps :hover indefinitely.
		Context->ProcessMouseLeave();
	}

	SLeafWidget::OnMouseLeave(MouseEvent);
}

FReply SFolioCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
	if (!Context || Key == Rml::Input::KI_UNKNOWN)
	{
		return FReply::Unhandled();
	}

	// RmlUi returns false when it consumed the key, which is the opposite convention to Slate.
	const bool bConsumed = !Context->ProcessKeyDown(Key, Folio::ToRmlModifiers(KeyEvent.GetModifierKeys()));
	return bConsumed ? FReply::Handled() : FReply::Unhandled();
}

FReply SFolioCanvas::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
	if (!Context || Key == Rml::Input::KI_UNKNOWN)
	{
		return FReply::Unhandled();
	}

	Context->ProcessKeyUp(Key, Folio::ToRmlModifiers(KeyEvent.GetModifierKeys()));
	return FReply::Handled();
}

FReply SFolioCanvas::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& CharacterEvent)
{
	if (!Context)
	{
		return FReply::Unhandled();
	}

	const TCHAR Character = CharacterEvent.GetCharacter();
	if (Character < TEXT(' '))
	{
		// Control characters arrive as key events instead; passing them here would insert
		// them into text fields literally.
		return FReply::Unhandled();
	}

	Context->ProcessTextInput(static_cast<Rml::Character>(Character));
	return FReply::Handled();
}

#include "SFolioCanvas.h"

#include "FolioInsights.h"

#include "FolioDataModel.h"
#include "FolioObjectModel.h"
#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioInput.h"
#include "FolioLocalization.h"
#include "FolioReload.h"
#include "FolioUiSounds.h"
#include "FolioRmlUi.h"
#include "GameFramework/InputSettings.h"
#include "Misc/FileHelper.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/MeshUtilities.h>

namespace
{
	TArray<SFolioCanvas*> LiveCanvases;
}

const TArray<SFolioCanvas*>& SFolioCanvas::GetLiveCanvases()
{
	return LiveCanvases;
}

void SFolioCanvas::Construct(const FArguments& InArgs)
{
	DesiredSize = InArgs._DesiredSize;
	PendingDocumentPath = InArgs._DocumentPath;

	LiveCanvases.AddUnique(this);
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

	InputModel.Create(Context);

	if (!PendingDocumentPath.IsEmpty())
	{
		LoadDocument(PendingDocumentPath);
	}
}

SFolioCanvas::~SFolioCanvas()
{
	LiveCanvases.RemoveSingleSwap(this);
	DestroyContext();
}

bool SFolioCanvas::RouteGamepadKeyDown(const FKeyEvent& KeyEvent)
{
	return OnKeyDown(FGeometry(), KeyEvent).IsEventHandled();
}

bool SFolioCanvas::RouteGamepadKeyUp(const FKeyEvent& KeyEvent)
{
	return OnKeyUp(FGeometry(), KeyEvent).IsEventHandled();
}

bool SFolioCanvas::RouteGamepadAnalog(const FAnalogInputEvent& AnalogEvent)
{
	return OnAnalogValueChanged(FGeometry(), AnalogEvent).IsEventHandled();
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
		InputModel.Release();
		ObjectModels.Empty();
		Context = nullptr;
		Document = nullptr;
		return;
	}

	// Closing a document is deferred until the next Update, so the context has to outlive the
	// close. Removing the context destroys everything in it outright, which is what we want.
	Sounds.Detach();
	InputModel.Release();
	ObjectModels.Empty();
	Document = nullptr;
	Rml::RemoveContext(Context->GetName());
	Context = nullptr;
	DataModel.Reset();
}

void SFolioCanvas::LoadDocument(const FString& RelativePath)
{
	if (!Context)
	{
		return;
	}

	if (Document)
	{
		Sounds.Detach();
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

	// Reopening a document is what anyone does after editing its art, so this is the moment to
	// notice that a file on disk has moved on. RmlUi caches a texture by path forever otherwise,
	// and a re-baked atlas keeps rendering as it was until the editor restarts.
	if (FFolioModule::IsAvailable())
	{
		if (FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface())
		{
			if (const int32 Reloaded = Renderer->ReloadChangedTextures())
			{
				UE_LOG(LogFolio, Log, TEXT("reloaded %d changed texture(s)"), Reloaded);
			}
		}
	}

	MountDataModel(RelativePath);

	Document = Context->LoadDocument(Folio::ToRmlString(RelativePath));
	if (!Document)
	{
		UE_LOG(LogFolio, Error, TEXT("could not load document '%s'"), *RelativePath);
		return;
	}

	// Before the first update, so translated text is laid out rather than reflowed a frame later.
	// Data views already exist by now - they are built as the markup is parsed - which is what lets
	// the pass recognise a bound element and leave it alone.
	Folio::Loc::ApplyToDocument(Document, RelativePath);

	Document->Show();
	Sounds.Attach(Document);
	InputModel.SetDocument(Document);
	UE_LOG(LogFolio, Log, TEXT("loaded '%s'"), *RelativePath);
}

void SFolioCanvas::ReloadDocument()
{
	if (!Context || PendingDocumentPath.IsEmpty())
	{
		return;
	}

	const Folio::Reload::FDocumentState State = Folio::Reload::CaptureDocumentState(Context, Document);

	// Without clearing these, an edited stylesheet reached through @import is served from cache and
	// the reload appears to do nothing at all.
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();
	Rml::ReleaseTextures();
	Rml::ReleaseCompiledGeometry();

	LoadDocument(PendingDocumentPath);
	if (!Document)
	{
		return;
	}

	// Layout has to exist before focus and scroll can be put back, and the restore itself changes
	// both, so the state lands one update later.
	Context->Update();
	Folio::Reload::RestoreDocumentState(Document, State);
	Context->Update();
}

void SFolioCanvas::ReloadStyleSheets()
{
	Folio::Reload::ReloadStyleSheets(Context);
}

bool SFolioCanvas::HasObjectModel(const FString& ModelName) const
{
	return ObjectModels.ContainsByPredicate(
		[&ModelName](const TUniquePtr<FFolioObjectModel>& Model)
		{
			return Model.IsValid() && Model->GetModelName() == ModelName;
		});
}

void SFolioCanvas::MountDataModel(const FString& RelativePath)
{
	// Mock data is a design-time stand-in and has no business in a retail build. Left in, a screen
	// whose model the game forgot to bind would silently render invented values instead of failing
	// visibly, and the mock would occupy the name the real view model wanted.
#if !UE_BUILD_SHIPPING
	if (!Folio::MockDataEnabled())
	{
		return;
	}

	// Once per context. A model cannot be added to a context that already has one under the same
	// name, and a document reload keeps the values it was mounted with.
	if (DataModel.IsValid())
	{
		return;
	}

	const FString& ContentRoot = FFolioModule::Get().GetContentRoot();

	FString Markup;
	if (!FFileHelper::LoadFileToString(Markup, *(ContentRoot / RelativePath)))
	{
		return;
	}

	const FString ModelName = FFolioDataModel::DiscoverModelName(Markup);
	if (ModelName.IsEmpty())
	{
		return;
	}

	// The reserved model is mounted with the context and is not a game model, so it has no mock
	// file and nothing is missing when there isn't one.
	if (ModelName == UTF8_TO_TCHAR(Folio::InputModel::Name()))
	{
		return;
	}

	// Game code got there first, and it wins. RmlUi refuses a second model under the same name, so
	// mounting a mock here is what would make BindObjectModel fail rather than the other way round.
	if (HasObjectModel(ModelName))
	{
		return;
	}

	const FString MockPath = FFolioDataModel::FindMockFile(ContentRoot, ModelName);
	if (MockPath.IsEmpty())
	{
		UE_LOG(LogFolio, Warning,
			TEXT("'%s' declares data-model=\"%s\" and nothing is bound, so its bindings render as literal ")
			TEXT("{{ }}. Provide the model from game code, or put mock values in ui/mock/%s.json."),
			*RelativePath, *ModelName, *ModelName);
		return;
	}

	DataModel = MakeUnique<FFolioDataModel>();
	if (!DataModel->Mount(Context, ModelName, MockPath))
	{
		DataModel.Reset();
	}
#endif
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
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_Update);
	SCOPE_CYCLE_COUNTER(STAT_FolioUpdate);

	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!Context)
	{
		return;
	}

	UpdateContextSize(AllottedGeometry);

	// Before the context update, so a value read this frame lays out this frame. Only sources with
	// no change notification do any work here, and a model whose source was collected stops on its
	// own rather than being removed - RmlUi owns it, and a document is still bound to its last values.
	for (const TUniquePtr<FFolioObjectModel>& Model : ObjectModels)
	{
		Model->Tick();
	}

	// A held stick becomes repeated presses here, where there is a clock. Suppressed while a text
	// field has focus, for the same reason letters are not directions there.
	if (!IsTakingText())
	{
		if (const Rml::Input::KeyIdentifier Nav = Analog.Advance(InDeltaTime);
			Nav != Rml::Input::KI_UNKNOWN)
		{
			SendNavigation(Nav, 0);
		}
	}

	// Slate's layout scale is Unreal's UI scaling; RmlUi expresses the same thing as a
	// density independent pixel ratio, which is what dp units resolve against.
	Context->SetDensityIndependentPixelRatio(AllottedGeometry.Scale);

	// Backgrounds carry their own antialiasing because Slate composites straight into the
	// swapchain with no multisampling to soften a rounded corner. The generator works in
	// document units, so it needs one rendered pixel expressed in them.
	const float RenderScale = AllottedGeometry.GetAccumulatedRenderTransform().GetMatrix().GetScale().GetVector().GetMax();
	Rml::MeshUtilities::SetBackgroundAntialiasWidth(RenderScale > KINDA_SMALL_NUMBER ? 1.0f / RenderScale : 1.0f);

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
	TRACE_CPUPROFILER_EVENT_SCOPE(Folio_Render);
	SCOPE_CYCLE_COUNTER(STAT_FolioRender);

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

	// Only a real move counts. Slate resends the cursor position on its own, and a stationary
	// mouse would otherwise take the device back from the gamepad every frame.
	if (!MouseEvent.GetCursorDelta().IsNearlyZero())
	{
		NoteDevice(Folio::EInputDevice::Mouse);
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

	NoteDevice(Folio::EInputDevice::Mouse);

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
	if (!Context)
	{
		return FReply::Unhandled();
	}

	NoteDevice(Folio::DeviceForKey(KeyEvent.GetKey()));

	const int32 Modifiers = Folio::ToRmlModifiers(KeyEvent.GetModifierKeys());
	const bool bClaimed = ClaimsKey(KeyEvent.GetKey());

	// The literal key first, so a text field still receives W as a letter. Only when nothing is
	// taking text does a letter get to mean a direction, which is what lets the arrows, WASD and
	// IJKL all navigate at once without any of them breaking typing.
	if (const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
		Key != Rml::Input::KI_UNKNOWN)
	{
		// An arrow with navigation switched off still belongs to a text field, and nowhere else.
		const bool bArrow = Key == Rml::Input::KI_UP || Key == Rml::Input::KI_DOWN
			|| Key == Rml::Input::KI_LEFT || Key == Rml::Input::KI_RIGHT;
		if (bArrow && !Folio::ArrowsNavigate() && !IsTakingText())
		{
			return FReply::Unhandled();
		}

		const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);
		return bConsumed || bClaimed ? FReply::Handled() : FReply::Unhandled();
	}

	if (IsTakingText())
	{
		return FReply::Unhandled();
	}

	int32 Extra = 0;
	if (const Rml::Input::KeyIdentifier Nav = Folio::ToNavigationKey(KeyEvent.GetKey(), Extra);
		Nav != Rml::Input::KI_UNKNOWN)
	{
		SendNavigation(Nav, Modifiers | Extra);
		return FReply::Handled();
	}

	return bClaimed ? FReply::Handled() : FReply::Unhandled();
}

bool SFolioCanvas::ClaimsKey(const FKey& Key) const
{
	// Two keys always escape, whatever has focus. The console is how a document that has gone wrong
	// gets closed, and Escape is how PIE gets stopped when the console cannot be opened.
	if (Key == EKeys::Escape)
	{
		return false;
	}

	if (const UInputSettings* Settings = UInputSettings::GetInputSettings())
	{
		if (Settings->ConsoleKeys.Contains(Key))
		{
			return false;
		}
	}

	// A focused text field owns the keyboard outright: a letter is a letter, not a movement key.
	// RmlUi delivers characters through ProcessTextInput rather than ProcessKeyDown, so the press
	// itself reads as unconsumed and would otherwise walk the character while a name is typed.
	if (IsTakingText())
	{
		return true;
	}

	// Otherwise only navigation. RmlUi reports a direction it could not act on - already at the edge
	// of a list - as unconsumed, and letting that through turns the pawn behind the menu. Worse, in a
	// CommonUI project the game receiving input makes the action router reapply its input config,
	// which reassigns focus away from the document, and then no further input arrives at all.
	return Folio::ActionForKey(Key) != EFolioNavAction::None;
}

FReply SFolioCanvas::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& AnalogEvent)
{
	if (!Context)
	{
		return FReply::Unhandled();
	}

	// Only recorded here. A stick reports a position every frame, so turning it into presses is
	// Tick's job, where there is a clock to measure the repeat against.
	Analog.SetAxis(AnalogEvent.GetKey(), AnalogEvent.GetAnalogValue());

	if (FMath::Abs(AnalogEvent.GetAnalogValue()) >= Folio::FAnalogNavigator::Deadzone)
	{
		NoteDevice(Folio::EInputDevice::Gamepad);
	}

	return FReply::Unhandled();
}

void SFolioCanvas::OnFocusLost(const FFocusEvent& FocusEvent)
{
	// A stick held while focus moves elsewhere would otherwise keep repeating into this document.
	Analog.Reset();
	SLeafWidget::OnFocusLost(FocusEvent);
}

bool SFolioCanvas::SendNavigation(const Rml::Input::KeyIdentifier Key, const int32 Modifiers)
{
	if (!Context)
	{
		return false;
	}

	// Down and up together. RmlUi navigates on the press, but an element left thinking a key is
	// still held misbehaves the next time the same key arrives.
	const bool bConsumed = !Context->ProcessKeyDown(Key, Modifiers);
	Context->ProcessKeyUp(Key, Modifiers);
	return bConsumed;
}

bool SFolioCanvas::BindObjectModel(const FString& ModelName, UObject* Source, const bool bPollEveryFrame)
{
	if (!Context)
	{
		return false;
	}

	TUniquePtr<FFolioObjectModel> Model = MakeUnique<FFolioObjectModel>();
	if (!Model->Mount(Context, ModelName, Source, bPollEveryFrame))
	{
		return false;
	}

	ObjectModels.Add(MoveTemp(Model));
	return true;
}

Folio::EInputDevice SFolioCanvas::GetLastInputDevice() const
{
	return FFolioModule::Get().GetInputDevice();
}

void SFolioCanvas::NoteDevice(const Folio::EInputDevice Device)
{
	// Held by the module, not here: two documents open at once must not disagree about whether to
	// draw a keyboard prompt, and only one of them receives the key that changed it.
	FFolioModule::Get().SetInputDevice(Device);
}

/** True while a text field has focus, so letters are text rather than directions. */
bool SFolioCanvas::IsTakingText() const
{
	if (!Context)
	{
		return false;
	}

	const Rml::Element* Focus = Context->GetFocusElement();
	if (Focus == nullptr)
	{
		return false;
	}

	const Rml::String FocusTag = Focus->GetTagName();
	if (FocusTag == "textarea")
	{
		return true;
	}

	if (FocusTag != "input")
	{
		return false;
	}

	// The tag alone is not enough: a checkbox, radio or range slider is an input that takes no
	// text, and treating it as one blocks WASD and IJKL for as long as it has focus. RmlUi defaults
	// a type-less input to text.
	const Rml::String Type = Focus->GetAttribute<Rml::String>("type", "text");
	return Type == "text" || Type == "password";
}

FReply SFolioCanvas::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	const Rml::Input::KeyIdentifier Key = Folio::ToRmlKey(KeyEvent.GetKey());
	if (!Context || Key == Rml::Input::KI_UNKNOWN)
	{
		// Paired with the press: a release reaching the game after its press did not leaves
		// Enhanced Input holding an action down that nothing will ever complete.
		return ClaimsKey(KeyEvent.GetKey()) ? FReply::Handled() : FReply::Unhandled();
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

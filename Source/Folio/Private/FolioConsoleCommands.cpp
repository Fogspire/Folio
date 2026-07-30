#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "FolioModule.h"
#include "FolioBindTest.h"
#include "FolioDebugger.h"
#include "FolioLocalization.h"
#include "FolioNavBindings.h"
#include "FolioWorldDocument.h"
#include "FolioRenderInterface.h"
#include "SFolioCanvas.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/SWindow.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

// None of this exists in a retail build. Every command here puts a document on screen, drives it or
// reloads it, which is development tooling: shipping it would leave a player able to open a test
// document over the game. UFolioBindTest is the exception and stays compiled, because a UCLASS inside
// a preprocessor block is not something UnrealHeaderTool handles - it generates the reflection code
// regardless and the link then fails. An unreferenced class registration costs nothing.
#if !UE_BUILD_SHIPPING

/**
 * Dev-only commands for putting a document on screen without any game code.
 *
 * folio.Show ui/screens/settings.rml
 * folio.Hide
 */
namespace
{
	TSharedPtr<SFolioCanvas> ActiveCanvas;
	TSharedPtr<SWidget> ActiveContainer;

	UGameViewportClient* FindViewport()
	{
		return GEngine ? GEngine->GameViewport : nullptr;
	}

	void HideDocument()
	{
		UGameViewportClient* Viewport = FindViewport();
		if (Viewport && ActiveContainer.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(ActiveContainer.ToSharedRef());
		}

		ActiveContainer.Reset();
		ActiveCanvas.Reset();
	}

	void ShowDocument(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogFolio, Warning, TEXT("usage: folio.Show <path relative to the project directory>"));
			return;
		}

		UGameViewportClient* Viewport = FindViewport();
		if (!Viewport)
		{
			UE_LOG(LogFolio, Warning, TEXT("no game viewport; run this in PIE or in a game"));
			return;
		}

		HideDocument();

		ActiveCanvas = SNew(SFolioCanvas).DocumentPath(Args[0]);

		// Wrapped in a weak widget so the viewport holding it does not keep the canvas alive
		// past folio.Hide, which would leave its RmlUi context rendering.
		ActiveContainer = SNew(SWeakWidget).PossiblyNullContent(ActiveCanvas);

		// Above the game, below UMG added later at a higher order, which is what makes the
		// compositing question answerable at all.
		Viewport->AddViewportWidgetContent(ActiveContainer.ToSharedRef(), 100);
	}

	TSharedPtr<SWindow> ActiveWindow;

	void CloseWindow()
	{
		if (ActiveWindow.IsValid())
		{
			ActiveWindow->RequestDestroyWindow();
			ActiveWindow.Reset();
		}
	}

	/**
	 * Shows a document in its own window rather than in the game viewport.
	 *
	 * Needs no game world and no PIE session, which makes it the quickest way to look at a
	 * document, and the only way when something else in the project stops the game booting.
	 */
	void ShowWindow(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogFolio, Warning, TEXT("usage: folio.ShowWindow <path> [width] [height]"));
			return;
		}

		CloseWindow();

		FVector2D Size(1280.0, 720.0);
		if (Args.Num() >= 3)
		{
			Size = FVector2D(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		}

		ActiveWindow = SNew(SWindow)
			.Title(FText::FromString(FString::Printf(TEXT("Folio - %s"), *Args[0])))
			.ClientSize(Size)
			.SupportsMaximize(true)
			.SupportsMinimize(true)
			[
				SNew(SFolioCanvas)
					.DocumentPath(Args[0])
					.DesiredSize(Size)
			];

		FSlateApplication::Get().AddWindow(ActiveWindow.ToSharedRef());
	}

	/**
	 * Picks up art edited since it was loaded, without reopening the document.
	 *
	 * Reopening already does this for files whose timestamp moved. The command exists for the
	 * case that does not: a file rewritten with the same timestamp, or a texture that came out
	 * wrong and needs re-reading to be sure of what is on disk.
	 */
	void ReloadTextures(const TArray<FString>& Args)
	{
		if (!FFolioModule::IsAvailable())
		{
			return;
		}

		FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface();
		if (!Renderer)
		{
			UE_LOG(LogFolio, Warning, TEXT("no render interface; Folio has not started"));
			return;
		}

		const bool bForce = Args.Num() > 0 && Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase);
		const int32 Reloaded = Renderer->ReloadChangedTextures(bForce);

		UE_LOG(LogFolio, Log, TEXT("released %d texture(s)%s"), Reloaded,
			bForce ? TEXT("") : TEXT("; pass 'all' to release every one"));
	}

	/**
	 * The Phase 3 gate, run as one command so it is repeatable rather than described.
	 *
	 * Shows a document over the game viewport with a plain Slate widget below it and another
	 * above, at the orders UMG uses, then screenshots. That is the whole claim being made: a
	 * document renders in a running game and composites in the right place in the widget stack -
	 * over the world and whatever the game drew, under anything added later at a higher order.
	 *
	 *   folio.Gate ui/examples/panel.rml
	 */
	TSharedPtr<SWidget> GateBelow;
	TSharedPtr<SWidget> GateAbove;
	TSharedPtr<SWidget> GateDocument;
	FTSTicker::FDelegateHandle GateTicker;

	/** A marker beside the document rather than across the screen, so the world stays visible. */
	TSharedRef<SWidget> MakeGateBar(const FLinearColor Colour, const FVector2D Offset)
	{
		return SNew(SBox)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(FMargin(Offset.X, Offset.Y, 0.0f, 0.0f))
			[
				SNew(SBox)
					.WidthOverride(760.0f)
					.HeightOverride(28.0f)
					[
						SNew(SImage).ColorAndOpacity(Colour)
					]
			];
	}

	void HideGate()
	{
		if (GateTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GateTicker);
			GateTicker.Reset();
		}

		UGameViewportClient* Viewport = FindViewport();
		if (Viewport)
		{
			if (GateBelow.IsValid()) { Viewport->RemoveViewportWidgetContent(GateBelow.ToSharedRef()); }
			if (GateAbove.IsValid()) { Viewport->RemoveViewportWidgetContent(GateAbove.ToSharedRef()); }
			if (GateDocument.IsValid()) { Viewport->RemoveViewportWidgetContent(GateDocument.ToSharedRef()); }
		}

		GateBelow.Reset();
		GateAbove.Reset();
		GateDocument.Reset();
	}

	/**
	 * The Phase 3 gate: a document in a running game, composited in the right place.
	 *
	 *   folio.Gate [document] [shot]
	 *
	 * Boxed rather than full screen, so the world stays visible behind and beside it - the point
	 * is what the document composites *with*. A marker sits above the box at viewport order 50
	 * and another at 200, with the document at 100 between them: the 200 one must cover the
	 * document's top edge and the 50 one must not.
	 *
	 * Pass `shot` to screenshot after a few seconds. Off by default because a screenshot request
	 * is not something to fire into an editor session unasked.
	 */
	void RunGate(const TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			HideGate();
			return;
		}

		const FString Document = Args.Num() > 0 ? Args[0] : TEXT("ui/examples/panel.rml");
		const bool bShot = Args.ContainsByPredicate([](const FString& Arg)
			{ return Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase); });

		HideGate();

		// Polled: -ExecCmds fires during engine init, before there is a viewport to put anything
		// in, and a level load clears viewport widgets anyway. Typed into a running session this
		// fires on the first tick.
		double Shown = -1.0;
		GateTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Document, bShot, Shown](float) mutable -> bool
			{
				UGameViewportClient* Viewport = FindViewport();
				if (!Viewport || !Viewport->GetWorld() || !Viewport->GetWorld()->HasBegunPlay())
				{
					return true;
				}

				if (Shown < 0.0)
				{
					GateBelow = MakeGateBar(FLinearColor(0.05f, 0.6f, 0.2f, 1.0f), FVector2D(40.0, 60.0));
					GateAbove = MakeGateBar(FLinearColor(0.8f, 0.1f, 0.6f, 0.75f), FVector2D(40.0, 60.0));

					// The document, boxed and offset so the game is visible around it.
					GateDocument = SNew(SBox)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						.Padding(FMargin(40.0f, 60.0f, 0.0f, 0.0f))
						[
							SNew(SBox)
								.WidthOverride(760.0f)
								.HeightOverride(520.0f)
								[
									SNew(SFolioCanvas)
										.DocumentPath(Document)
										.DesiredSize(FVector2D(760.0, 520.0))
								]
						];

					Viewport->AddViewportWidgetContent(GateBelow.ToSharedRef(), 50);
					Viewport->AddViewportWidgetContent(GateDocument.ToSharedRef(), 100);
					Viewport->AddViewportWidgetContent(GateAbove.ToSharedRef(), 200);

					UE_LOG(LogFolio, Display,
						TEXT("folio.Gate: '%s' is up. The magenta bar must cover its top edge and the "
							"green one must not. folio.Gate off to remove it."), *Document);

					Shown = FPlatformTime::Seconds();
					if (!bShot)
					{
						GateTicker.Reset();
						return false;
					}
					return true;
				}

				if (FPlatformTime::Seconds() - Shown < 4.0)
				{
					return true;
				}

				FScreenshotRequest::RequestScreenshot(TEXT("FolioGate"), /*bShowUI*/ true,
					/*bAddFilenameSuffix*/ false);
				UE_LOG(LogFolio, Display, TEXT("folio.Gate: screenshot requested"));

				GateTicker.Reset();
				return false;
			}), 0.0f);
	}

	/**
	 * A boxed, focused document for driving navigation by hand in PIE.
	 *
	 *   folio.NavTest            the bundled document
	 *   folio.NavTest <path>     any other one
	 *   folio.NavTest off
	 *
	 * What it adds over folio.Show is keyboard focus. Slate sends key and analog events to the
	 * focused widget, and in PIE that is the viewport, so a document put up by folio.Show renders
	 * correctly and navigates not at all - which reads as Folio being broken rather than as nobody
	 * having asked for focus.
	 *
	 * Boxed and offset, never full screen: the world stays visible so it is obvious the game is
	 * still running underneath. Nothing is screenshotted and no ticker keeps running once it is up.
	 */
	TSharedPtr<SFolioCanvas> NavCanvas;
	TSharedPtr<SWidget> NavContainer;
	FTSTicker::FDelegateHandle NavTicker;

	APlayerController* FirstPlayer()
	{
		UGameViewportClient* Viewport = FindViewport();
		UWorld* World = Viewport ? Viewport->GetWorld() : nullptr;
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	void HideNavTest()
	{
		if (NavTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(NavTicker);
			NavTicker.Reset();
		}

		if (NavCanvas.IsValid())
		{
			// Before anything else, or reclaiming would fight the router for a document on its way out.
			NavCanvas->SetKeepFocus(false);
		}

		UGameViewportClient* Viewport = FindViewport();
		if (Viewport && NavContainer.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(NavContainer.ToSharedRef());

			// The game gets its input back, or the session is left taking none at all.
			if (APlayerController* Player = FirstPlayer())
			{
				Player->SetInputMode(FInputModeGameOnly());
				Player->SetShowMouseCursor(false);
			}
			else if (FSlateApplication::IsInitialized())
			{
				if (const TSharedPtr<SWidget> GameWidget = Viewport->GetGameViewportWidget())
				{
					FSlateApplication::Get().SetAllUserFocus(GameWidget, EFocusCause::SetDirectly);
				}
			}
		}

		NavContainer.Reset();
		NavCanvas.Reset();
	}

	void RunNavTest(const TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			HideNavTest();
			return;
		}

		const FString Document = Args.Num() > 0 ? Args[0] : TEXT("/folio/dev/nav_test.rml");

		HideNavTest();

		// Polled for the same reason folio.Gate is: -ExecCmds runs before a viewport exists, and a
		// level load clears viewport widgets. Typed into a running session this fires next tick.
		NavTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Document](float) -> bool
			{
				UGameViewportClient* Viewport = FindViewport();
				if (!Viewport || !Viewport->GetWorld() || !Viewport->GetWorld()->HasBegunPlay())
				{
					return true;
				}

				const FVector2D Size(760.0, 560.0);

				NavCanvas = SNew(SFolioCanvas)
					.DocumentPath(Document)
					.DesiredSize(Size);

				NavContainer = SNew(SBox)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.Padding(FMargin(40.0f, 60.0f, 0.0f, 0.0f))
					[
						SNew(SBox)
							.WidthOverride(Size.X)
							.HeightOverride(Size.Y)
							[
								SNew(SWeakWidget).PossiblyNullContent(NavCanvas)
							]
					];

				Viewport->AddViewportWidgetContent(NavContainer.ToSharedRef(), 100);

				// Every user, not just user 0: a gamepad in PIE is often a second Slate user, and
				// focusing only the keyboard's user leaves the sticks going to the game.
				FSlateApplication::Get().SetAllUserFocus(NavCanvas, EFocusCause::SetDirectly);

				// CommonUI's action router hands focus back to the game viewport whenever it
				// reapplies its input config, which SetInputMode below and any gamepad press both
				// cause. This is what stops that quietly killing every subsequent press.
				NavCanvas->SetKeepFocus(true);

				// The input mode is what makes the mouse usable and stops the pawn reading the same
				// input the menu is reading. Slate focus alone does neither: the viewport keeps the
				// cursor captured and hidden, so there is nothing to click with.
				if (APlayerController* Player = FirstPlayer())
				{
					FInputModeGameAndUI Mode;
					Mode.SetWidgetToFocus(NavCanvas);
					Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
					Mode.SetHideCursorDuringCapture(false);
					Player->SetInputMode(Mode);
					Player->SetShowMouseCursor(true);
				}

				UE_LOG(LogFolio, Display,
					TEXT("folio.NavTest: '%s' has focus. Arrows, WASD, IJKL, d-pad and the sticks all ")
					TEXT("navigate. 'folio.NavTest off' closes it and gives focus back to the game; ")
					TEXT("Escape stops PIE, which is the way out if the console cannot be opened."),
					*Document);

				NavTicker.Reset();
				return false;
			}), 0.0f);
	}

	/**
	 * Proves the view model bridge, with a plain UObject whose values move every frame.
	 *
	 *   folio.MvvmTest [off]
	 */
	TSharedPtr<SFolioCanvas> MvvmCanvas;
	TSharedPtr<SWidget> MvvmContainer;
	TStrongObjectPtr<UFolioBindTest> MvvmSource;
	FTSTicker::FDelegateHandle MvvmTicker;
	/** One per `set` still waiting for the document, so closing the test cancels them. */
	TArray<FTSTicker::FDelegateHandle> PendingSets;

	void HideMvvmTest()
	{
		for (FTSTicker::FDelegateHandle& Pending : PendingSets)
		{
			if (Pending.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(Pending);
			}
		}
		PendingSets.Empty();

		if (MvvmTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(MvvmTicker);
			MvvmTicker.Reset();
		}

		UGameViewportClient* Viewport = FindViewport();
		if (Viewport && MvvmContainer.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(MvvmContainer.ToSharedRef());
		}

		MvvmContainer.Reset();
		MvvmCanvas.Reset();
		MvvmSource.Reset();
	}

	/**
	 * Drives a control the way a drag would, so the write path can be checked without a hand on a mouse.
	 *
	 * Setting the value attribute is what RmlUi's own slider does when it moves, so this exercises the
	 * real route: control, change event, data controller, our variable definition, the property.
	 */
	void SetMvvmControlNow(const FString& ElementId, const FString& Value)
	{
		Rml::Element* Element = MvvmCanvas->GetDocument()->GetElementById(Folio::ToRmlString(ElementId));
		if (!Element)
		{
			UE_LOG(LogFolio, Warning, TEXT("no element '%s' in the test document"), *ElementId);
			return;
		}

		// A checkbox's state is its `checked` attribute, not its value, so a click toggles that.
		const Rml::String Type = Element->GetAttribute<Rml::String>("type", "");
		if (Type == "checkbox" || Type == "radio")
		{
			const bool bOn = Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
			if (bOn)
			{
				Element->SetAttribute("checked", "");
			}
			else
			{
				Element->RemoveAttribute("checked");
			}
		}
		else if (Rml::ElementFormControl* Control = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
		{
			Control->SetValue(Folio::ToRmlString(Value));
		}

		// The data controller listens for `change` carrying a `value` parameter, which is what a widget
		// dispatches when a person moves it. SetValue alone updates the control and announces nothing, so
		// without this the model never hears about a programmatic change.
		Rml::Dictionary Parameters;
		Parameters["value"] = Rml::Variant(Folio::ToRmlString(Value));
		Element->DispatchEvent(Rml::EventId::Change, Parameters);

		MvvmCanvas->GetContext()->Update();

		UFolioBindTest* Source = MvvmSource.Get();
		UE_LOG(LogFolio, Display,
			TEXT("folio.MvvmTest: after setting %s to '%s' the object holds Volume=%d Gamma=%.3f ")
			TEXT("bSubtitles=%s PlayerName='%s' Health=%.3f"),
			*ElementId, *Value, Source->Volume, Source->Gamma,
			Source->bSubtitles ? TEXT("true") : TEXT("false"), *Source->PlayerName, Source->Health);
	}

	/**
	 * Waits for the document before driving it.
	 *
	 * folio.MvvmTest defers opening until a game viewport exists, so a `set` issued in the same breath -
	 * from -ExecCmds, or by anyone typing quickly - would otherwise arrive before there is anything to
	 * drive and report only that the test is not open.
	 */
	void SetMvvmControl(const FString& ElementId, const FString& Value)
	{
		if (MvvmCanvas.IsValid() && MvvmCanvas->GetDocument() && MvvmSource.IsValid())
		{
			SetMvvmControlNow(ElementId, Value);
			return;
		}

		if (!MvvmTicker.IsValid())
		{
			UE_LOG(LogFolio, Warning, TEXT("folio.MvvmTest is not open"));
			return;
		}

		const double Deadline = FPlatformTime::Seconds() + 30.0;
		PendingSets.Add(FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[ElementId, Value, Deadline](float) -> bool
			{
				if (MvvmCanvas.IsValid() && MvvmCanvas->GetDocument() && MvvmSource.IsValid())
				{
					SetMvvmControlNow(ElementId, Value);
					return false;
				}

				if (FPlatformTime::Seconds() > Deadline)
				{
					UE_LOG(LogFolio, Warning, TEXT("gave up waiting for the test document to open"));
					return false;
				}

				return true;
			}), 0.0f));
	}

	void RunMvvmTest(const TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			HideMvvmTest();
			return;
		}

		if (Args.Num() >= 3 && Args[0].Equals(TEXT("set"), ESearchCase::IgnoreCase))
		{
			SetMvvmControl(Args[1], Args[2]);
			return;
		}

		HideMvvmTest();

		MvvmTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[](float) -> bool
			{
				UGameViewportClient* Viewport = FindViewport();
				if (!Viewport || !Viewport->GetWorld() || !Viewport->GetWorld()->HasBegunPlay())
				{
					return true;
				}

				if (!MvvmCanvas.IsValid())
				{
					const FVector2D Size(520.0, 400.0);

					MvvmSource.Reset(NewObject<UFolioBindTest>());
					MvvmCanvas = SNew(SFolioCanvas).DesiredSize(Size);

					// Bound before the document loads, so its bindings resolve on the first layout
					// rather than rendering literal braces for a frame.
					MvvmCanvas->BindObjectModel(TEXT("test"), MvvmSource.Get());
					MvvmCanvas->LoadDocument(TEXT("/folio/dev/mvvm_test.rml"));

					MvvmContainer = SNew(SBox)
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						.Padding(FMargin(830.0f, 60.0f, 0.0f, 0.0f))
						[
							SNew(SBox)
								.WidthOverride(Size.X)
								.HeightOverride(Size.Y)
								[
									SNew(SWeakWidget).PossiblyNullContent(MvvmCanvas)
								]
						];

					Viewport->AddViewportWidgetContent(MvvmContainer.ToSharedRef(), 100);

					UE_LOG(LogFolio, Display,
						TEXT("folio.MvvmTest: bound a UObject as data model 'test'. The values must move ")
						TEXT("and Hidden must stay unbound. folio.MvvmTest off to close."));
				}

				if (MvvmSource.IsValid())
				{
					MvvmSource->Advance();
				}

				return true;
			}), 0.0f);
	}

	/**
	 * Rebinding by hand, so a binding table is not needed to try one.
	 *
	 *   folio.Nav.Bind Q Left
	 *   folio.Nav.Bind Q None      unbinds it
	 *   folio.Nav.Bindings         what every action currently answers to
	 *   folio.Nav.ResetBindings
	 */
	void BindNavKey(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogFolio, Warning,
				TEXT("usage: folio.Nav.Bind <key> <Up|Down|Left|Right|Confirm|Back|NextTab|PreviousTab|None>"));
			return;
		}

		const FKey Key(*Args[0]);
		if (!Key.IsValid())
		{
			UE_LOG(LogFolio, Warning, TEXT("'%s' is not a key name"), *Args[0]);
			return;
		}

		const UEnum* Enum = StaticEnum<EFolioNavAction>();
		const int64 Value = Enum->GetValueByNameString(Args[1]);
		if (Value == INDEX_NONE)
		{
			UE_LOG(LogFolio, Warning, TEXT("'%s' is not a navigation action"), *Args[1]);
			return;
		}

		const EFolioNavAction Action = static_cast<EFolioNavAction>(Value);
		UFolioNavLibrary::SetNavBinding(Key, Action);

		UE_LOG(LogFolio, Display, TEXT("%s now means %s"), *Key.ToString(),
			*Enum->GetNameStringByValue(Value));
	}

	void ListNavBindings()
	{
		const UEnum* Enum = StaticEnum<EFolioNavAction>();

		for (int32 Index = 1; Index < Enum->NumEnums() - 1; ++Index)
		{
			const EFolioNavAction Action = static_cast<EFolioNavAction>(Enum->GetValueByIndex(Index));

			TArray<FString> Names;
			for (const FKey& Key : UFolioNavLibrary::GetKeysForNavAction(Action))
			{
				Names.Add(Key.ToString());
			}

			UE_LOG(LogFolio, Display, TEXT("%-12s %s"), *Enum->GetNameStringByIndex(Index),
				Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("nothing"));
		}
	}

	void ResetNavBindings()
	{
		UFolioNavLibrary::ResetNavBindings();
		UE_LOG(LogFolio, Display, TEXT("navigation bindings cleared; the built-in schemes apply"));
	}

	/**
	 * Which document the debugger should inspect.
	 *
	 * A focused canvas is the one being looked at. The fallback to the last registered is what
	 * makes the command work straight after folio.Show, before anything has taken focus.
	 */
	SFolioCanvas* PickCanvas()
	{
		const TArray<SFolioCanvas*>& Live = SFolioCanvas::GetLiveCanvases();
		if (Live.Num() == 0)
		{
			return nullptr;
		}

		for (SFolioCanvas* Canvas : Live)
		{
			if (Canvas->HasAnyUserFocus().IsSet())
			{
				return Canvas;
			}
		}

		return Live.Last();
	}

	/**
	 * RmlUi's own element inspector.
	 *
	 *   folio.Debugger [on|off]
	 */
	void RunDebugger(const TArray<FString>& Args)
	{
		const FString Mode = Args.Num() > 0 ? Args[0].ToLower() : FString();

		if (Mode == TEXT("off"))
		{
			Folio::Debug::SetVisible(false);
			return;
		}

		SFolioCanvas* Canvas = PickCanvas();
		if (!Canvas)
		{
			UE_LOG(LogFolio, Warning, TEXT("no Folio document is open; open one first"));
			return;
		}

		// Attaching to a canvas that already hosts it is free, so this doubles as the re-target
		// when a second document has since opened.
		const bool bWasVisible = Folio::Debug::Host() == Canvas && Folio::Debug::IsVisible();
		if (!Folio::Debug::Attach(Canvas))
		{
			return;
		}

		const bool bVisible = Mode == TEXT("on") || !bWasVisible;
		Folio::Debug::SetVisible(bVisible);

		if (bVisible)
		{
			UE_LOG(LogFolio, Display,
				TEXT("debugger open on the document. It needs a cursor to click with, so run ")
				TEXT("'folio.MouseMode on' if the game still has the mouse. 'folio.Debugger off' hides it."));
		}
	}

	/**
	 * A document on a surface in the world, in front of the camera.
	 *
	 *   folio.WorldTest [document] [inscene|aftertonemap]
	 *   folio.WorldTest off
	 *
	 * The mode argument is what makes the two routes comparable: the same document, in the same place, drawn
	 * either inside the temporal resolve or after it.
	 */
	TWeakObjectPtr<AActor> WorldTestActor;
	/** Set while a report is waiting for the component's first recording. */
	FTSTicker::FDelegateHandle WorldReportTicker;

	void HideWorldTest()
	{
		if (WorldReportTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(WorldReportTicker);
			WorldReportTicker.Reset();
		}

		if (AActor* Actor = WorldTestActor.Get())
		{
			Actor->Destroy();
		}
		WorldTestActor.Reset();
	}

	/**
	 * What the document recorded, and what the same document gives Slate.
	 *
	 * Both counts come from the same RmlUi render pass over the same markup, so they have to agree. A
	 * recording that silently dropped a draw would otherwise only show up as something missing from a
	 * surface, which is far harder to read than a number that does not match.
	 */
	void ReportWorldTestNow()
	{
		for (const UFolioWorldDocument* Surface : UFolioWorldDocument::GetLive())
		{
			const TSharedPtr<const FFolioDrawList> Recorded = Surface->GetDrawList();
			UE_LOG(LogFolio, Display,
				TEXT("folio.WorldTest: '%s' recorded %d draw(s), %d triangle(s) at %dx%d"),
				*Surface->DocumentPath,
				Recorded.IsValid() ? Recorded->Num() : 0,
				Recorded.IsValid() ? Recorded->CountTriangles() : 0,
				Surface->GetDocumentSize().X, Surface->GetDocumentSize().Y);
		}
	}

	/**
	 * Waits for a recording before reporting one.
	 *
	 * The component records during its tick, so a report issued in the same breath - from -ExecCmds, or
	 * by anyone typing quickly - would say zero and mean nothing.
	 */
	void ReportWorldTest()
	{
		if (UFolioWorldDocument::GetLive().IsEmpty())
		{
			UE_LOG(LogFolio, Warning, TEXT("no world document is open"));
			return;
		}

		if (WorldReportTicker.IsValid())
		{
			return;
		}

		const double Deadline = FPlatformTime::Seconds() + 10.0;
		WorldReportTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Deadline](float) -> bool
			{
				const TArray<UFolioWorldDocument*>& Live = UFolioWorldDocument::GetLive();
				const bool bRecorded = Live.ContainsByPredicate(
					[](const UFolioWorldDocument* Surface)
					{
						const TSharedPtr<const FFolioDrawList> Recorded = Surface->GetDrawList();
						return Recorded.IsValid() && !Recorded->IsEmpty();
					});

				if (bRecorded || FPlatformTime::Seconds() > Deadline)
				{
					ReportWorldTestNow();
					WorldReportTicker.Reset();
					return false;
				}

				return true;
			}), 0.0f);
	}

	void RunWorldTest(const TArray<FString>& Args)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			HideWorldTest();
			return;
		}

		if (Args.Num() > 0 && Args[0].Equals(TEXT("report"), ESearchCase::IgnoreCase))
		{
			ReportWorldTest();
			return;
		}

		UWorld* World = FindViewport() ? FindViewport()->GetWorld() : nullptr;
		if (!World)
		{
			UE_LOG(LogFolio, Warning, TEXT("no world; run this in PIE or in a game"));
			return;
		}

		HideWorldTest();

		// The mode is a token rather than a second positional, so the document stays first whether or not
		// one is given. Comparing the two routes is the whole reason this command takes it at all.
		EFolioWorldDrawMode Mode = EFolioWorldDrawMode::AfterTonemap;
		FString Document;

		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("inscene"), ESearchCase::IgnoreCase)
				|| Arg.Equals(TEXT("scene"), ESearchCase::IgnoreCase))
			{
				Mode = EFolioWorldDrawMode::InScene;
			}
			else if (Arg.Equals(TEXT("aftertonemap"), ESearchCase::IgnoreCase)
				|| Arg.Equals(TEXT("tonemap"), ESearchCase::IgnoreCase))
			{
				Mode = EFolioWorldDrawMode::AfterTonemap;
			}
			else if (Document.IsEmpty())
			{
				Document = Arg;
			}
		}

		if (Document.IsEmpty())
		{
			Document = TEXT("/folio/dev/idle_animation.rml");
		}

		// Two metres ahead of the camera, so it is visible without anyone having to place an actor.
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		if (const APlayerController* Player = FirstPlayer())
		{
			Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
		}
		else
		{
			// Said out loud because the alternative presents as the surface being invisible: at the origin
			// it is usually inside the level or behind the camera, and nothing about that says why.
			UE_LOG(LogFolio, Warning,
				TEXT("no player controller, so the surface goes at the world origin rather than in front of "
					"the camera. Possess a pawn and run this again to place it."));
		}

		// Pitched up and turned off axis, so it stands rather than lying flat and is seen at an angle. That
		// makes a projection error obvious: a surface square to the camera hides skew and foreshortening.
		//
		// The rotation goes on the spawn transform rather than onto the component afterwards: setting it on
		// a component that is neither registered nor the root does not survive being made the root.
		const FTransform SpawnAt(FRotator(90.0, 45.0, 0.0),
			ViewLocation + ViewRotation.Vector() * 200.0);

		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnAt);
		if (!Actor)
		{
			UE_LOG(LogFolio, Warning, TEXT("could not spawn the world document actor"));
			return;
		}

		UFolioWorldDocument* Surface = NewObject<UFolioWorldDocument>(Actor);
		Surface->DocumentPath = Document;
		// Before registering: the mode decides which space the recording's colours are in, and the first
		// recording happens on the first tick after registration.
		Surface->DrawMode = Mode;
		Actor->SetRootComponent(Surface);
		Surface->RegisterComponent();

		WorldTestActor = Actor;

		const FIntPoint Size = Surface->GetDocumentSize();
		UE_LOG(LogFolio, Display,
			TEXT("folio.WorldTest: '%s' on a %.0fx%.0f surface, laid out at %dx%d, drawn %s. ")
			TEXT("'folio.WorldTest report' says what it recorded; 'folio.WorldTest off' removes it."),
			*Document, Surface->WorldSize.X, Surface->WorldSize.Y, Size.X, Size.Y,
			Mode == EFolioWorldDrawMode::InScene
				? TEXT("in scene, so temporal antialiasing, bloom and depth of field all reach it")
				: TEXT("after the tonemapper, where temporal antialiasing cannot reach it"));
	}

	/**
	 * Reports what every keyed string in a document resolved to.
	 *
	 *   folio.Loc.Report [document]
	 */
	void ReportLocalisation(const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			// Opened here rather than required to be on screen already, so the check can be run
			// against any document in the project from one command.
			ShowDocument(Args);
		}

		SFolioCanvas* Canvas = PickCanvas();
		if (!Canvas || !Canvas->GetDocument())
		{
			UE_LOG(LogFolio, Warning, TEXT("no document is open; pass one, or open one first"));
			return;
		}

		Folio::Loc::ReportDocument(Canvas->GetDocument(), Canvas->GetDocumentPath());
	}

	/**
	 * Menu mouse rather than gameplay mouse, so a document can be clicked.
	 *
	 *   folio.MouseMode [on|off]
	 */
	void SetMouseMode(const TArray<FString>& Args)
	{
		APlayerController* Player = FirstPlayer();
		if (!Player)
		{
			UE_LOG(LogFolio, Warning, TEXT("no player controller; run this in PIE or in a game"));
			return;
		}

		const bool bOn = Args.Num() == 0 || (Args[0] != TEXT("off") && Args[0] != TEXT("0"));
		if (bOn)
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			Player->SetInputMode(Mode);
			Player->SetShowMouseCursor(true);
		}
		else
		{
			Player->SetInputMode(FInputModeGameOnly());
			Player->SetShowMouseCursor(false);
		}

		UE_LOG(LogFolio, Display, TEXT("mouse mode %s"), bOn ? TEXT("on") : TEXT("off"));
	}

	/**
	 * Tears both down before RmlUi does. These are statics, so without this they are destroyed
	 * during DLL unload, long after the module has shut RmlUi down.
	 */
	struct FShutdownHook
	{
		FShutdownHook()
		{
			if (FFolioModule::IsAvailable())
			{
				FFolioModule::Get().OnPreShutdown().AddStatic(&FShutdownHook::Close);
			}
		}

		static void Close()
		{
			Folio::Debug::Detach();
			HideWorldTest();
			CloseWindow();
			HideDocument();
			HideGate();
			HideNavTest();
			HideMvvmTest();
		}
	};

	FAutoConsoleCommand ShowWindowCommand(
		TEXT("folio.ShowWindow"),
		TEXT("Opens an .rml document in its own window. Works in the editor with no PIE session."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ShowWindow));

	FAutoConsoleCommand CloseWindowCommand(
		TEXT("folio.CloseWindow"),
		TEXT("Closes the window opened by folio.ShowWindow."),
		FConsoleCommandDelegate::CreateStatic(&CloseWindow));

	FAutoConsoleCommand ShowCommand(
		TEXT("folio.Show"),
		TEXT("Loads an .rml document into the game viewport. Path is relative to the project directory."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ShowDocument));

	FAutoConsoleCommand HideCommand(
		TEXT("folio.Hide"),
		TEXT("Removes the document shown by folio.Show."),
		FConsoleCommandDelegate::CreateStatic(&HideDocument));

	FAutoConsoleCommand GateCommand(
		TEXT("folio.Gate"),
		TEXT("folio.Gate [document] [shot|off]. Shows a boxed document between two viewport markers, "
			"so compositing is visible against the world. The Phase 3 gate."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunGate));

	FAutoConsoleCommand NavTestCommand(
		TEXT("folio.NavTest"),
		TEXT("folio.NavTest [document|off]. Opens a boxed document in PIE and gives it keyboard focus, "
			"so arrows, WASD, IJKL and the gamepad can be tried by hand."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunNavTest));

	FAutoConsoleCommand MvvmTestCommand(
		TEXT("folio.MvvmTest"),
		TEXT("folio.MvvmTest [off|set <id> <value>]. Binds a UObject whose values move every frame as a "
			"data model, which is the view model path without needing a view model. 'set' drives a "
			"control the way a drag would, so the write-back path can be checked from the console."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunMvvmTest));

	FAutoConsoleCommand BindNavCommand(
		TEXT("folio.Nav.Bind"),
		TEXT("folio.Nav.Bind <key> <action>. Binds a key to a navigation action at runtime. "
			"Actions: Up Down Left Right Confirm Back NextTab PreviousTab None."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&BindNavKey));

	FAutoConsoleCommand ListNavCommand(
		TEXT("folio.Nav.Bindings"),
		TEXT("Lists the keys every navigation action currently answers to, built-in schemes included."),
		FConsoleCommandDelegate::CreateStatic(&ListNavBindings));

	FAutoConsoleCommand ResetNavCommand(
		TEXT("folio.Nav.ResetBindings"),
		TEXT("Forgets every runtime navigation binding, leaving the built-in schemes."),
		FConsoleCommandDelegate::CreateStatic(&ResetNavBindings));

	FAutoConsoleCommand DebuggerCommand(
		TEXT("folio.Debugger"),
		TEXT("folio.Debugger [on|off]. Opens RmlUi's element inspector on the focused document: "
			"element tree, computed style, event log and every data model in the context."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunDebugger));

	FAutoConsoleCommand WorldTestCommand(
		TEXT("folio.WorldTest"),
		TEXT("folio.WorldTest [document|off|report]. Puts a document on a surface in the world in front "
			"of the camera and records its geometry. Draws nothing: the pass that draws a world document "
			"is not written yet. 'report' says how much it recorded."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunWorldTest));

	FAutoConsoleCommand LocReportCommand(
		TEXT("folio.Loc.Report"),
		TEXT("folio.Loc.Report [document]. Lists every data-loc key in a document and whether it "
			"resolved to a translation or fell back to its source text."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReportLocalisation));

	FAutoConsoleCommand MouseModeCommand(
		TEXT("folio.MouseMode"),
		TEXT("folio.MouseMode [on|off]. Shows the cursor and stops the game consuming the mouse, "
			"so a document can be clicked."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetMouseMode));

	FAutoConsoleCommand ReloadTexturesCommand(
		TEXT("folio.ReloadTextures"),
		TEXT("Releases cached textures whose file has changed, so re-baked art appears. 'all' releases every one."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReloadTextures));

	FShutdownHook ShutdownHook;
}

#endif // !UE_BUILD_SHIPPING

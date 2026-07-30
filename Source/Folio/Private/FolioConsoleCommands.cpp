#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "FolioModule.h"
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
			CloseWindow();
			HideDocument();
			HideGate();
			HideNavTest();
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

	FAutoConsoleCommand ReloadTexturesCommand(
		TEXT("folio.ReloadTextures"),
		TEXT("Releases cached textures whose file has changed, so re-baked art appears. 'all' releases every one."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReloadTextures));

	FShutdownHook ShutdownHook;
}

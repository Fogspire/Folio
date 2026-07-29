#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
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

	TSharedRef<SWidget> MakeGateBar(const FLinearColor Colour)
	{
		return SNew(SBox)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			.HeightOverride(48.0f)
			[
				SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SImage).ColorAndOpacity(Colour)
					]
			];
	}

	FTSTicker::FDelegateHandle GateTicker;

	void RunGate(const TArray<FString>& Args)
	{
		const FString Document = Args.Num() > 0 ? Args[0] : TEXT("ui/examples/panel.rml");
		const float Settle = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 4.0f;

		if (GateTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GateTicker);
			GateTicker.Reset();
		}

		// Polled rather than run on the spot. -ExecCmds fires during engine init, long before
		// there is a game viewport to put anything in, and a level load clears viewport widgets
		// anyway - so the first attempt would be thrown away even if it worked.
		double Shown = -1.0;
		GateTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Document, Settle, Shown](float) mutable -> bool
			{
				UGameViewportClient* Viewport = FindViewport();
				if (!Viewport || !Viewport->GetWorld() || !Viewport->GetWorld()->HasBegunPlay())
				{
					return true;
				}

				if (Shown < 0.0)
				{
					// Below and above the document, at the orders game UI uses. Slate draws
					// viewport widgets in order, so a document composited into the wrong place
					// would be covered by one of these.
					GateBelow = MakeGateBar(FLinearColor(0.05f, 0.35f, 0.15f, 1.0f));
					GateAbove = MakeGateBar(FLinearColor(0.6f, 0.1f, 0.5f, 0.6f));

					Viewport->AddViewportWidgetContent(GateBelow.ToSharedRef(), 50);
					ShowDocument({ Document });
					Viewport->AddViewportWidgetContent(GateAbove.ToSharedRef(), 200);

					UE_LOG(LogFolio, Display, TEXT("folio.Gate: showing '%s'"), *Document);
					Shown = FPlatformTime::Seconds();
					return true;
				}

				// Frames for the document to load its stylesheet, generate its glyphs and draw,
				// and for whatever loading screen the game puts up to go away. A screenshot before
				// that catches something else entirely and proves nothing.
				if (FPlatformTime::Seconds() - Shown < double(Settle))
				{
					return true;
				}

				FScreenshotRequest::RequestScreenshot(TEXT("FolioGate"), /*bShowUI*/ true,
					/*bAddFilenameSuffix*/ false);
				UE_LOG(LogFolio, Display, TEXT("folio.Gate: screenshot requested"));

				GateTicker.Reset();
				return false;
			}), 0.25f);
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
			GateBelow.Reset();
			GateAbove.Reset();
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
		TEXT("folio.Gate [document] [seconds]. Shows a document between two viewport widgets and "
			"screenshots it once the game is up. The Phase 3 gate."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunGate));

	FAutoConsoleCommand ReloadTexturesCommand(
		TEXT("folio.ReloadTextures"),
		TEXT("Releases cached textures whose file has changed, so re-baked art appears. 'all' releases every one."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReloadTextures));

	FShutdownHook ShutdownHook;
}

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "SFolioCanvas.h"
#include "Framework/Application/SlateApplication.h"
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

	FAutoConsoleCommand ReloadTexturesCommand(
		TEXT("folio.ReloadTextures"),
		TEXT("Releases cached textures whose file has changed, so re-baked art appears. 'all' releases every one."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ReloadTextures));

	FShutdownHook ShutdownHook;
}

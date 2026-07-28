#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

FOLIO_API DECLARE_LOG_CATEGORY_EXTERN(LogFolio, Log, All);

class FFolioSystemInterface;
class FFolioFileInterface;
class FFolioRenderInterface;

/**
 * Owns RmlUi's process-wide state.
 *
 * RmlUi has one global instance: its interfaces are set once, Rml::Initialise is called once,
 * and every context in the process shares them. That maps onto a module rather than onto any
 * widget or world, so the lifetime lives here.
 */
class FOLIO_API FFolioModule : public IModuleInterface
{
public:
	static FFolioModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FFolioModule>(TEXT("Folio"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("Folio"));
	}

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** False if RmlUi failed to initialise; every Folio widget renders nothing in that case. */
	bool IsRmlInitialised() const { return bRmlInitialised; }

	/** Absolute path to the directory documents resolve against. */
	const FString& GetContentRoot() const { return ContentRoot; }

	/**
	 * The one render interface for the process.
	 *
	 * RmlUi holds a single global renderer, so widgets do not own one: each installs its own
	 * Slate element list around its render pass instead. Safe because Slate paints serially
	 * on the game thread, so only one widget is ever inside that pass.
	 */
	FFolioRenderInterface* GetRenderInterface() const { return RenderInterface.Get(); }

	/**
	 * Fires before RmlUi is torn down, so anything holding a context can drop it first.
	 *
	 * Widgets outlive the module: a canvas parked in a window is destroyed during static
	 * teardown, well after ShutdownModule, and a context destroyed by Rml::Shutdown is gone
	 * by then. Calling RemoveContext at that point walks a destroyed hash map.
	 */
	FSimpleMulticastDelegate& OnPreShutdown() { return PreShutdownDelegate; }

private:
	void RegisterFonts();

	TUniquePtr<FFolioSystemInterface> SystemInterface;
	TUniquePtr<FFolioFileInterface> FileInterface;
	TUniquePtr<FFolioRenderInterface> RenderInterface;

	FSimpleMulticastDelegate PreShutdownDelegate;

	FString ContentRoot;
	FString PluginRoot;
	bool bRmlInitialised = false;
};

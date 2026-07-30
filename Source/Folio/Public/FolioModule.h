#pragma once

#include "FolioUiSounds.h"

#include "FolioCore/FolioInputState.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

FOLIO_API DECLARE_LOG_CATEGORY_EXTERN(LogFolio, Log, All);

class FFolioSystemInterface;
class FFolioFileInterface;
class FFolioRenderInterface;

namespace Folio { class FFolioFontEngine; }

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

	/** Absolute path to the plugin, which is where Folio's own stylesheets and fonts live. */
	const FString& GetPluginRoot() const { return PluginRoot; }

	/** Also the hot reload dependency list: it records every file RmlUi has opened. */
	FFolioFileInterface* GetFileInterface() const { return FileInterface.Get(); }

	/**
	 * The one render interface for the process.
	 *
	 * RmlUi holds a single global renderer, so widgets do not own one: each installs its own
	 * Slate element list around its render pass instead. Safe because Slate paints serially
	 * on the game thread, so only one widget is ever inside that pass.
	 */
	FFolioRenderInterface* GetRenderInterface() const { return RenderInterface.Get(); }

	/** The text engine, or null if RmlUi failed to start. */
	Folio::FFolioFontEngine* GetFontEngine() const { return FontEngine.Get(); }

	/** Where generated fields are kept between runs. */
	const FString& GetFontCacheDir() const { return FontCacheDir; }

	/**
	 * Fires before RmlUi is torn down, so anything holding a context can drop it first.
	 *
	 * Widgets outlive the module: a canvas parked in a window is destroyed during static
	 * teardown, well after ShutdownModule, and a context destroyed by Rml::Shutdown is gone
	 * by then. Calling RemoveContext at that point walks a destroyed hash map.
	 */
	FSimpleMulticastDelegate& OnPreShutdown() { return PreShutdownDelegate; }

	/**
	 * Fires on focus change, confirm, back and invalid, for whoever wants to play a sound.
	 *
	 * Folio plays nothing: it has no business owning a sound asset, and what a game wants to hear
	 * is a game decision. This exists because threading sound through a component library after
	 * the fact means touching every component, and a delegate now costs nothing.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnUiSound, Folio::EUiSound);
	FOnUiSound& OnUiSound() { return UiSoundDelegate; }

	/**
	 * What the player last used, across every document in the process.
	 *
	 * Global rather than per-canvas because it answers a question about the player, not about a
	 * screen: a HUD and a pause menu open at once must not disagree about whether to draw a
	 * keyboard prompt. Set from real input; documents also receive it as body classes and through
	 * the reserved 'folio' data model, so most UI never needs to call this.
	 */
	Folio::EInputDevice GetInputDevice() const { return InputDevice; }
	void SetInputDevice(Folio::EInputDevice Device);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnInputDevice, Folio::EInputDevice);
	FOnInputDevice& OnInputDeviceChanged() { return InputDeviceDelegate; }

	/**
	 * Which gamepad face button confirms; the other one means back.
	 *
	 * A game exposes this to its settings and may also default it per platform. Folio has no
	 * platform opinion of its own.
	 */
	Folio::EConfirmButton GetConfirmButton() const;
	void SetConfirmButton(Folio::EConfirmButton Button);
	FSimpleMulticastDelegate& OnConfirmButtonChanged();

private:
	void RegisterFonts();

	TUniquePtr<FFolioSystemInterface> SystemInterface;
	TUniquePtr<FFolioFileInterface> FileInterface;
	TUniquePtr<FFolioRenderInterface> RenderInterface;
	/// The distance-field text engine, and where its generated fields are kept between runs.
	TUniquePtr<Folio::FFolioFontEngine> FontEngine;
	TUniquePtr<class FFolioFontCache> FontCache;
	TUniquePtr<class FFolioCoreHost> CoreHost;

	/**
	 * Draws world-space documents after the tonemapper.
	 *
	 * A scene view extension is shared rather than owned outright: the renderer holds it for as long as a
	 * frame is in flight, so it cannot be a unique pointer the module destroys at will.
	 */
	TSharedPtr<class FFolioWorldPass> WorldPass;
#if WITH_EDITOR
	TUniquePtr<class FFolioHotReload> HotReload;
#endif

	FSimpleMulticastDelegate PreShutdownDelegate;
	FOnUiSound UiSoundDelegate;
	FOnInputDevice InputDeviceDelegate;
	// Keyboard rather than Unknown: the body classes already say kbm before any input arrives, so
	// reporting "unknown" through the data model only ever contradicts them.
	Folio::EInputDevice InputDevice = Folio::EInputDevice::Keyboard;

	FString ContentRoot;
	FString PluginRoot;
	FString FontCacheDir;
	bool bRmlInitialised = false;
};

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
	FFolioModule();
	// Out of line, because every member below is a TUniquePtr to a type this header only forward
	// declares. An implicit destructor has to be instantiated wherever one happens to be needed, and
	// there it needs all of them complete - which is not a property a consumer can be expected to
	// maintain, and fails as a wall of errors naming the header rather than the file that caused it.
	virtual ~FFolioModule() override;

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

	/**
	 * The content root's UI tree, holding fonts, mock data, localisation and the documents.
	 *
	 * `UI` to match Content and every other directory Unreal puts beside it, but a lower-case one
	 * is taken if that is what is there. Windows cannot tell them apart and Linux can, so a
	 * project written on one and packaged on the other would otherwise lose its fonts.
	 */
	FString GetUiRoot() const;

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
	using FOnUiSound = Folio::FOnUiSound;
	FOnUiSound& OnUiSound() { return UiSoundDelegate; }

	/**
	 * Fires as a `<text>` reveals a character, naming whatever its `reveal-sound` names.
	 *
	 * Separate from the four moments above because it is not one of them: the sound is chosen by
	 * the document rather than by what the player did, and a line of dialogue picks its own voice.
	 * At most once a frame however fast the text is typing, because a click per character at sixty
	 * characters a second is a buzz rather than a sound.
	 */
	using FOnTextRevealSound = Folio::FOnTextRevealSound;
	FOnTextRevealSound& OnTextRevealSound() { return TextRevealSoundDelegate; }

	/**
	 * Fires as a `<text>` finishes revealing, whether it ran to the end or was skipped.
	 *
	 * Process-wide, because the element that finished does not know what is hosting it. UFolioWidget
	 * subscribes and filters by owning document, which is the form a screen actually wants.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnTextRevealed, Rml::Element* /* Element */);
	FOnTextRevealed& OnTextRevealed() { return TextRevealedDelegate; }

	/**
	 * Reads every `UI/terms/*.terms`, replacing whatever was loaded before.
	 *
	 * Called at startup and again when one of those files changes. A term's text is resolved
	 * through the `Folio.Terms` namespace, so a translated name arrives here rather than at each
	 * use, which is the point of the table.
	 */
	void LoadTerms();

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
	FOnTextRevealSound TextRevealSoundDelegate;
	FOnTextRevealed TextRevealedDelegate;
	FOnInputDevice InputDeviceDelegate;
	// Keyboard rather than Unknown: the body classes already say kbm before any input arrives, so
	// reporting "unknown" through the data model only ever contradicts them.
	Folio::EInputDevice InputDevice = Folio::EInputDevice::Keyboard;

	FString ContentRoot;
	FString PluginRoot;
	FString FontCacheDir;
	bool bRmlInitialised = false;
};

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

FOLIO_API DECLARE_LOG_CATEGORY_EXTERN(LogFolio, Log, All);

class FFolioSystemInterface;
class FFolioFileInterface;

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

private:
	void RegisterFonts();

	TUniquePtr<FFolioSystemInterface> SystemInterface;
	TUniquePtr<FFolioFileInterface> FileInterface;

	FString ContentRoot;
	bool bRmlInitialised = false;
};

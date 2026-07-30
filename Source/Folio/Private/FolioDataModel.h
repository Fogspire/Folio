#pragma once

#include "CoreMinimal.h"
#include "FolioRmlUi.h"

#include <RmlUi/Core/DataModelHandle.h>

class FJsonValue;

namespace Rml { class Context; }

namespace Folio
{
	/**
	 * Whether design-time mock data may be mounted, from folio.MockData.
	 *
	 * On by default. Worth turning off to see what a screen looks like with nothing bound, which is
	 * what a player gets if the game forgets to bind the model. Never true in Shipping, where mock
	 * mounting is not compiled at all.
	 */
	bool MockDataEnabled();
}

/**
 * Mounts a JSON file as a data model, with no compile-time structs.
 *
 * A document that declares data-model and finds nothing bound renders its bindings as literal
 * {{ }}, which reads as broken markup rather than as missing data. This is what the dev console
 * commands use to fill a screen from the same ui/mock file the preview tool reads.
 *
 * RmlUi re-walks a data address from its bound variable on every update, so each bound name
 * needs an address that outlives the model. Every top-level key therefore keeps its own shared
 * value rather than pointing into one root that a reload would replace.
 */
class FFolioDataModel
{
public:
	FFolioDataModel();
	~FFolioDataModel();

	/** Reads the file and creates the model. Must happen before the document is loaded. */
	bool Mount(Rml::Context* Context, const FString& ModelName, const FString& JsonPath);

	bool IsMounted() const { return bMounted; }

	/** The model a document declares, read from the markup: the DOM does not exist yet. */
	static FString DiscoverModelName(const FString& DocumentText);

	/** ui/mock/<model>.json under the content root, or empty if there is none. */
	static FString FindMockFile(const FString& ContentRoot, const FString& ModelName);

private:
	class FDefinitions;

	TUniquePtr<FDefinitions> Definitions;
	/** One allocation per bound name, because RmlUi holds the address. */
	TMap<FString, TSharedPtr<FJsonValue>> Slots;
	Rml::DataModelHandle Handle;
	bool bMounted = false;
};

#pragma once

#include "CoreMinimal.h"

/**
 * The plugin's own Slate style, which exists for one icon.
 *
 * A toolbar entry needs an FSlateIcon, and an FSlateIcon needs a registered style set: there is no
 * way to point one at a file on disk. So the plugin's Resources icon is registered here under a name
 * the toolbar can ask for.
 */
class FFolioEditorStyle
{
public:
	static void Register();
	static void Unregister();

	static FName GetStyleSetName();

	/** The toolbar button's icon. */
	static FName GetToolbarIconName() { return TEXT("Folio.Toolbar"); }
};

#pragma once

#include "CoreMinimal.h"

/** What the Folio toolbar menu does. */
namespace FolioLevelActions
{
	/**
	 * Selects every actor in the open level holding a document component with a path that is wrong.
	 *
	 * Map Check already lists them one entry at a time, which is the right shape for reading and the
	 * wrong one for fixing: a level with a directory rename behind it has dozens, and they all need
	 * the same edit. Selecting them puts the whole set in the details panel at once, where one change
	 * to the shared property fixes every one.
	 */
	void SelectActorsWithBrokenDocuments();
}

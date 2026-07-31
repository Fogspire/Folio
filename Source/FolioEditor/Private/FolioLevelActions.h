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

	/**
	 * Warns once if the project's UI tree will not be in a packaged build.
	 *
	 * Everything reads loose files until the moment it is packaged, so this is the one mistake that
	 * cannot be found by looking at the game: it is correct in the editor, correct standalone, and
	 * every document is missing in the build. Checked at startup rather than at cook, because a cook
	 * has nobody watching it and this is a one-line fix in Project Settings.
	 */
	void CheckStagedDirectories();

	/**
	 * Adds whatever the open level's documents need to the packaging staging list.
	 *
	 * The check above says what is wrong; this fixes it. Every document path in the level is resolved
	 * to the tree it lives in - the project's UI tree, or one of the plugin's mounts - and any tree
	 * not already staged is added to DefaultGame.ini. Roots rather than the documents' own
	 * directories, because a document pulls in stylesheets and art beside it.
	 */
	void StageDocumentsUsedInLevel();
}

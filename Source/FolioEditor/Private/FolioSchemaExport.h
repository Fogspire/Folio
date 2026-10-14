#pragma once

#include "CoreMinimal.h"

/** Writing view model schemas out of the classes a project already has. */
namespace FolioSchemaExport
{
	/**
	 * Writes UI/schema/<model>.json for every class bound as a view model this session.
	 *
	 * A document binds by name and Folio binds an object by reflection, so the class already is the
	 * description of what a document may ask for. Writing that down lets Folio Studio complete a
	 * binding as it is typed, flag one the class does not have, and keep the mock a document previews
	 * against matched to the class, all without the class being rewritten to suit a generator.
	 *
	 * The file is marked as reflected, which tells Studio the C++ decides the fields and their types.
	 * Sample values already in the file, or in the model's mock, are kept: those are the one part of a
	 * schema the class cannot supply.
	 *
	 * Only what has been bound is known, because the name a class is bound under exists nowhere but at
	 * the call that binds it. Play the screens once and their models are here.
	 */
	void ExportViewModelSchemas();

	/** @return Whether anything has been bound yet, which is what the export has to work from. */
	bool HasBoundViewModels();
}

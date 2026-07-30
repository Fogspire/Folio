#pragma once

#include "CoreMinimal.h"

namespace Folio
{
	/**
	 * Whether a document that has not changed may skip its update and its paint.
	 *
	 * On by default. `folio.Invalidation 0` makes every open document update and render every frame,
	 * which is what to try first if a document freezes: if the symptom goes away with it off, the
	 * dirty tracking missed something rather than the document being broken.
	 */
	bool InvalidationEnabled();

	/** Logs each update and when RmlUi next wants one, from folio.Invalidation.Trace. */
	bool InvalidationTracing();
}

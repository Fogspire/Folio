#include "FolioInvalidation.h"

#include "HAL/IConsoleManager.h"

namespace Folio
{
	namespace
	{
		TAutoConsoleVariable<bool> CVarInvalidation(
			TEXT("folio.Invalidation"),
			true,
			TEXT("Skip updating and rendering a document that has not changed. Off makes every open "
				"document run every frame, which is the first thing to try if one appears frozen."),
			ECVF_Default);

		TAutoConsoleVariable<bool> CVarTrace(
			TEXT("folio.Invalidation.Trace"),
			false,
			TEXT("Log every document update and how long RmlUi wants until the next one. A static "
				"document should say 'never, until input'; anything animating should say 0."),
			ECVF_Default);
	}

	bool InvalidationEnabled()
	{
		return CVarInvalidation.GetValueOnGameThread();
	}

	bool InvalidationTracing()
	{
		return CVarTrace.GetValueOnGameThread();
	}
}

#include "FolioCore/FolioTrace.h"

namespace Folio::Trace
{
	namespace
	{
		FHooks GHooks;
	}

	void SetHooks(const FHooks& InHooks)
	{
		GHooks = InHooks;
	}

	const FHooks& GetHooks()
	{
		return GHooks;
	}
}

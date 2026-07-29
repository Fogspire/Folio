#include "FolioCore/FolioHost.h"

namespace Folio
{
	namespace
	{
		IFolioHost* GFolioHost = nullptr;
	}

	void SetHost(IFolioHost* Host)
	{
		GFolioHost = Host;
	}

	IFolioHost* GetHost()
	{
		return GFolioHost;
	}

	void LogError(const std::string& Message)
	{
		if (GFolioHost != nullptr)
		{
			GFolioHost->LogError(Message);
		}
	}

	void LogWarning(const std::string& Message)
	{
		if (GFolioHost != nullptr)
		{
			GFolioHost->LogWarning(Message);
		}
	}
}

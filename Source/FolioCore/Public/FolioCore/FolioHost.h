#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Folio
{
	/// Reading files and reporting problems, supplied by whoever is hosting the core.
	///
	/// This exists because RmlUi is a static library. In Unreal it is linked into two modules -
	/// Folio and FolioCore - and each gets its own copy of RmlUi's globals. The module that calls
	/// Rml::SetFileInterface sets *its* copy, so Rml::GetFileInterface() from the other module
	/// returns null, and Rml::Log::Message from it goes to a system interface that was never
	/// installed. Fonts then fail to load and say nothing about why, which is a genuinely
	/// difficult silence to read.
	///
	/// So the core does not touch RmlUi's globals. It asks the host, exactly as it does for
	/// texture pages, the field cache and tracing.
	class IFolioHost
	{
	public:
		virtual ~IFolioHost() = default;

		/// Reads a whole file. The path is whatever the document referred to, so the host is also
		/// what resolves mounts like /folio/ and makes a packaged build work.
		virtual bool ReadFile(const std::string& Path, std::vector<unsigned char>& OutData) = 0;

		virtual void LogError(const std::string& Message) = 0;
		virtual void LogWarning(const std::string& Message) = 0;
	};

	/// Installed once, before anything is loaded. Without a host, faces cannot be read from disk
	/// and nothing is logged.
	FOLIOCORE_API void SetHost(IFolioHost* Host);
	FOLIOCORE_API IFolioHost* GetHost();

	/// Reports through the host if there is one. Safe to call before one is installed, which is
	/// the case for anything that runs during construction.
	FOLIOCORE_API void LogError(const std::string& Message);
	FOLIOCORE_API void LogWarning(const std::string& Message);

	/// printf into a string, for building the messages above.
	template <typename... TArgs>
	std::string Format(const char* Pattern, TArgs... Args)
	{
		const int Length = std::snprintf(nullptr, 0, Pattern, Args...);
		if (Length <= 0)
		{
			return Pattern;
		}

		std::vector<char> Buffer(static_cast<std::size_t>(Length) + 1);
		std::snprintf(Buffer.data(), Buffer.size(), Pattern, Args...);
		return std::string(Buffer.data(), static_cast<std::size_t>(Length));
	}
}

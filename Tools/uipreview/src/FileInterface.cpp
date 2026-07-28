#include "FileInterface.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace uipreview {

namespace {

std::string Normalise(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');
	std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return path;
}

} // namespace

RootedFileInterface::RootedFileInterface(std::string in_root) : root(std::move(in_root))
{
	std::replace(root.begin(), root.end(), '\\', '/');
	while (!root.empty() && root.back() == '/')
		root.pop_back();
}

std::string RootedFileInterface::Resolve(const std::string& path) const
{
	std::string normalised = path;
	std::replace(normalised.begin(), normalised.end(), '\\', '/');

	// Already absolute, so use it as given.
	if (normalised.size() > 1 && normalised[1] == ':')
		return normalised;

	while (!normalised.empty() && normalised.front() == '/')
		normalised.erase(0, 1);

	return root + "/" + normalised;
}

Rml::FileHandle RootedFileInterface::Open(const Rml::String& path)
{
	const std::string resolved = Resolve(path);
	FILE* fp = std::fopen(resolved.c_str(), "rb");
	if (!fp)
		return (Rml::FileHandle) nullptr;

	std::error_code ec;
	const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, ec);
	opened.insert(Normalise(ec ? resolved : canonical.string()));

	return (Rml::FileHandle)fp;
}

Rml::FileHandle RootedFileInterface::OpenWithRetry(const std::string& path, int timeout_ms)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	for (;;)
	{
		if (Rml::FileHandle handle = Open(path))
		{
			// A zero length file means the writer has not finished yet.
			std::fseek((FILE*)handle, 0, SEEK_END);
			const long size = std::ftell((FILE*)handle);
			if (size > 0)
			{
				std::fseek((FILE*)handle, 0, SEEK_SET);
				return handle;
			}
			Close(handle);
		}

		if (std::chrono::steady_clock::now() >= deadline)
			return (Rml::FileHandle) nullptr;

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void RootedFileInterface::Close(Rml::FileHandle file)
{
	if (file)
		std::fclose((FILE*)file);
}

size_t RootedFileInterface::Read(void* buffer, size_t size, Rml::FileHandle file)
{
	return std::fread(buffer, 1, size, (FILE*)file);
}

bool RootedFileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	return std::fseek((FILE*)file, offset, origin) == 0;
}

size_t RootedFileInterface::Tell(Rml::FileHandle file)
{
	return (size_t)std::ftell((FILE*)file);
}

} // namespace uipreview

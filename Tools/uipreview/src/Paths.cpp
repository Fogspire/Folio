#include "Paths.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include <windows.h>

namespace fs = std::filesystem;

namespace uipreview {

namespace {

std::string Forward(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');
	while (!path.empty() && path.back() == '/')
		path.pop_back();
	return path;
}

bool HasSlateFonts(const fs::path& engine_directory)
{
	std::error_code ec;
	return fs::exists(engine_directory / "Engine" / "Content" / "Slate" / "Fonts" / "Roboto-Regular.ttf", ec);
}

/** Walks up from a starting directory looking for an engine layout. */
std::string SearchUpwardsForEngine(fs::path directory)
{
	std::error_code ec;
	for (; !directory.empty(); directory = directory.parent_path())
	{
		if (HasSlateFonts(directory))
			return Forward((directory / "Engine").string());

		// A sibling engine checkout, which is how a source build is usually laid out.
		for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
		{
			if (entry.is_directory(ec) && HasSlateFonts(entry.path()))
				return Forward((entry.path() / "Engine").string());
		}

		if (!directory.has_relative_path())
			break;
	}
	return {};
}

} // namespace

std::string ExecutableDirectory()
{
	wchar_t buffer[MAX_PATH * 4] = {};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, (DWORD)(sizeof buffer / sizeof buffer[0]));
	if (length == 0)
		return {};

	const fs::path executable(std::wstring(buffer, length));
	return Forward(executable.parent_path().string());
}

std::string FolioRoot()
{
	if (const char* override_path = std::getenv("FOLIO_ROOT"))
		return Forward(override_path);

	// The executable is built to <Folio>/bin/win64/, so the plugin root is two levels up.
	const fs::path executable_directory(ExecutableDirectory());
	std::error_code ec;

	for (fs::path directory = executable_directory; !directory.empty(); directory = directory.parent_path())
	{
		if (fs::exists(directory / "rcss" / "folio.rcss", ec))
			return Forward(directory.string());
		if (!directory.has_relative_path())
			break;
	}

	return Forward(executable_directory.parent_path().parent_path().string());
}

std::string FindEngineDirectory(const std::string& explicit_directory)
{
	if (!explicit_directory.empty())
	{
		const fs::path given(explicit_directory);
		// Accept either the engine directory itself or the folder containing it.
		if (HasSlateFonts(given))
			return Forward((given / "Engine").string());
		return Forward(given.string());
	}

	if (const char* from_environment = std::getenv("UE_ENGINE_DIR"))
		return Forward(from_environment);

	if (std::string found = SearchUpwardsForEngine(fs::path(ExecutableDirectory())); !found.empty())
		return found;

	std::error_code ec;
	return SearchUpwardsForEngine(fs::current_path(ec));
}

} // namespace uipreview

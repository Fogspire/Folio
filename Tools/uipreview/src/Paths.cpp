#include "Paths.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

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

/** Reads a registered engine location out of the registry, keyed by an association GUID. */
std::string LookUpRegisteredEngine(const std::string& association)
{
#ifdef _WIN32
	for (const HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
	{
		HKEY key = nullptr;
		if (RegOpenKeyExA(root, "Software\\Epic Games\\Unreal Engine\\Builds", 0, KEY_READ, &key) != ERROR_SUCCESS)
			continue;

		char value[1024] = {};
		DWORD size = sizeof value;
		DWORD type = 0;
		const LSTATUS status = RegQueryValueExA(key, association.c_str(), nullptr, &type, (LPBYTE)value, &size);
		RegCloseKey(key);

		if (status == ERROR_SUCCESS && type == REG_SZ)
			return Forward(std::string(value, strnlen(value, sizeof value)));
	}
#else
	(void)association;
#endif
	return {};
}

/**
 * Resolves the engine a project is bound to, by walking up to the .uproject and reading its
 * EngineAssociation. That is either a registered build GUID or a path relative to the project,
 * and it is the only reliable answer when the engine lives on another drive entirely.
 */
std::string EngineFromUproject(fs::path directory)
{
	std::error_code ec;
	for (; !directory.empty(); directory = directory.parent_path())
	{
		fs::path uproject;
		for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec))
		{
			if (entry.is_regular_file(ec) && entry.path().extension() == ".uproject")
			{
				uproject = entry.path();
				break;
			}
		}

		if (!uproject.empty())
		{
			std::ifstream file(uproject);
			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

			const size_t key = text.find("\"EngineAssociation\"");
			if (key == std::string::npos)
				return {};
			const size_t open = text.find('"', text.find(':', key));
			const size_t close = text.find('"', open + 1);
			if (open == std::string::npos || close == std::string::npos)
				return {};

			std::string association = text.substr(open + 1, close - open - 1);
			if (association.empty())
				return {};

			if (association.front() == '{')
			{
				const std::string registered = LookUpRegisteredEngine(association);
				return registered.empty() ? std::string() : Forward((fs::path(registered) / "Engine").string());
			}

			// Otherwise a path, which the .uproject expresses relative to itself.
			const fs::path relative = fs::weakly_canonical(uproject.parent_path() / association, ec);
			return Forward((relative / "Engine").string());
		}

		if (!directory.has_relative_path())
			break;
	}
	return {};
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
#ifdef _WIN32
	wchar_t buffer[MAX_PATH * 4] = {};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, (DWORD)(sizeof buffer / sizeof buffer[0]));
	if (length == 0)
		return {};

	const fs::path executable(std::wstring(buffer, length));
	return Forward(executable.parent_path().string());
#else
	std::error_code ec;
	const fs::path executable = fs::read_symlink("/proc/self/exe", ec);
	return ec ? std::string() : Forward(executable.parent_path().string());
#endif
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

std::string UiDirectory(const std::string& root)
{
	std::error_code ec;
	const std::string upper = root + "/UI";
	if (fs::is_directory(fs::path(upper), ec))
		return upper;

	const std::string lower = root + "/ui";
	return fs::is_directory(fs::path(lower), ec) ? lower : upper;
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

	// The project's own association comes first: it is the engine this plugin is built
	// against, and it is frequently on a different drive to the project.
	if (std::string found = EngineFromUproject(fs::path(ExecutableDirectory())); !found.empty())
		return found;

	if (std::string found = SearchUpwardsForEngine(fs::path(ExecutableDirectory())); !found.empty())
		return found;

	std::error_code ec;
	return SearchUpwardsForEngine(fs::current_path(ec));
}

} // namespace uipreview

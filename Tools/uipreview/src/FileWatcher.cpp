#include "FileWatcher.h"

#include <algorithm>
#include <chrono>

#include <windows.h>

namespace uipreview {

namespace {

long long NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string Normalise(std::wstring wide, const std::string& directory)
{
	std::string narrow;
	narrow.reserve(wide.size());
	const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
	narrow.resize((size_t)needed);
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), narrow.data(), needed, nullptr, nullptr);

	std::string full = directory + "/" + narrow;
	std::replace(full.begin(), full.end(), '\\', '/');
	std::transform(full.begin(), full.end(), full.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return full;
}

} // namespace

FileWatcher::~FileWatcher()
{
	Stop();
}

bool FileWatcher::Start(const std::string& in_directory)
{
	directory = in_directory;
	std::replace(directory.begin(), directory.end(), '\\', '/');
	while (!directory.empty() && directory.back() == '/')
		directory.pop_back();

	stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!stop_event)
		return false;

	stopping = false;
	thread = std::thread(&FileWatcher::Run, this);
	return true;
}

void FileWatcher::Stop()
{
	if (!thread.joinable())
		return;

	stopping = true;
	if (stop_event)
		SetEvent((HANDLE)stop_event);
	thread.join();

	if (stop_event)
	{
		CloseHandle((HANDLE)stop_event);
		stop_event = nullptr;
	}
}

void FileWatcher::Run()
{
	std::wstring wide_directory;
	{
		const int needed = MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), (int)directory.size(), nullptr, 0);
		wide_directory.resize((size_t)needed);
		MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), (int)directory.size(), wide_directory.data(), needed);
	}

	const HANDLE handle = CreateFileW(wide_directory.c_str(), FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return;

	OVERLAPPED overlapped = {};
	overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	std::vector<unsigned char> buffer(64 * 1024);

	while (!stopping)
	{
		ResetEvent(overlapped.hEvent);

		// FILE_NAME matters as much as LAST_WRITE: editors save by writing a temporary file
		// and renaming it over the target, which arrives as a rename rather than a write.
		if (!ReadDirectoryChangesW(handle, buffer.data(), (DWORD)buffer.size(), TRUE,
				FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE, nullptr,
				&overlapped, nullptr))
		{
			break;
		}

		const HANDLE waits[] = {overlapped.hEvent, (HANDLE)stop_event};
		const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
		if (result != WAIT_OBJECT_0 || stopping)
			break;

		DWORD transferred = 0;
		if (!GetOverlappedResult(handle, &overlapped, &transferred, FALSE) || transferred == 0)
			continue;

		size_t offset = 0;
		for (;;)
		{
			const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
			const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));

			{
				std::lock_guard<std::mutex> lock(mutex);
				pending.insert(Normalise(name, directory));
				last_event_ms = NowMs();
			}

			if (info->NextEntryOffset == 0)
				break;
			offset += info->NextEntryOffset;
		}
	}

	CancelIo(handle);
	CloseHandle(overlapped.hEvent);
	CloseHandle(handle);
}

std::vector<std::string> FileWatcher::Drain(int debounce_ms)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (pending.empty())
		return {};

	// Trailing edge: wait until the burst has stopped, so one save is one reload.
	if (NowMs() - last_event_ms < debounce_ms)
		return {};

	std::vector<std::string> out(pending.begin(), pending.end());
	pending.clear();
	return out;
}

} // namespace uipreview

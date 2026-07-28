#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace uipreview {

/**
 * Watches a directory tree for changes, on a background thread.
 *
 * Notification based rather than mtime polling, for two reasons: a real UI tree is hundreds
 * of files, so polling is a stat storm every frame that still misses edits made between
 * ticks; and Phase 5 uses Unreal's IDirectoryWatcher, which is also notification based, so
 * behaviour found here carries over instead of having to be rediscovered.
 */
class FileWatcher {
public:
	~FileWatcher();

	bool Start(const std::string& directory);
	void Stop();

	/**
	 * Returns the paths changed since the last call, lowercased and forward slashed, once
	 * they have been quiet for the debounce interval. Editors write a temporary file and
	 * rename over the target, producing a burst of events for one logical save.
	 */
	std::vector<std::string> Drain(int debounce_ms = 120);

private:
	void Run();

	std::string directory;
	std::thread thread;
	std::atomic<bool> stopping {false};
	void* stop_event = nullptr;

	std::mutex mutex;
	std::set<std::string> pending;
	long long last_event_ms = 0;
};

} // namespace uipreview

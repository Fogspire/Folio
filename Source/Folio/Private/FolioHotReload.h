#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

// IDirectoryWatcher is a Developer module, so it does not exist in a game build. Iterating against a
// real build is what the -uidev loose-file override is for instead.
#if WITH_EDITOR

struct FFileChangeData;

/**
 * Watches the UI source tree and reloads open documents when a file under it changes.
 *
 * Granularity matters more than it looks: rebuilding a document loses everything the DOM was
 * holding, so a stylesheet edit reloads styles only and leaves focus, scroll and the data model
 * where they were. What decides is the set of files RmlUi actually opened, which the file interface
 * records, so there is no @import parser here to drift from RmlUi's own resolution rules.
 */
class FFolioHotReload
{
public:
	FFolioHotReload();
	~FFolioHotReload();

	/** Registers the watches. Safe to call twice; the second call does nothing. */
	void Start();
	void Stop();

	bool IsRunning() const { return Watches.Num() > 0; }

private:
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);

	/**
	 * Applies whatever accumulated, one tick after the first notification.
	 *
	 * A single save arrives as several notifications - the temporary file, the rename, an attribute
	 * change - and re-baking an atlas touches many files at once. Reloading per notification would
	 * rebuild the same document three times and race the editor's own write.
	 */
	bool Flush(float);

	struct FWatch
	{
		FString Directory;
		FDelegateHandle Handle;
	};

	TArray<FWatch> Watches;
	TSet<FString> Pending;
	FTSTicker::FDelegateHandle FlushHandle;
};

#endif

#pragma once

#include "FolioCore/MsdfCache.h"

#include <string>

namespace uipreview {

/**
 * Keeps generated fields in a directory beside the content root.
 *
 * Generating a field costs a few milliseconds, most of it resolving the outline's overlaps, so a
 * cold run over a screen's worth of text is most of a second. Every run after the first reads it
 * back instead.
 *
 * Deliberately not under ui/: it is derived data keyed by file contents, so it is disposable and
 * should not look like content someone is meant to edit or commit.
 */
class FileFontCache final : public Folio::IMsdfCacheStore {
public:
	explicit FileFontCache(std::string root);

	bool Read(const std::string& key, std::vector<unsigned char>& out_data) override;
	void Write(const std::string& key, const std::vector<unsigned char>& data) override;

	int Hits() const { return hits; }
	int Writes() const { return writes; }
	const std::string& Directory() const { return directory; }

private:
	std::string directory;
	int hits = 0;
	int writes = 0;
};

} // namespace uipreview

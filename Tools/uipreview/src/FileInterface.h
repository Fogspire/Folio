#pragma once

#include <RmlUi/Core/FileInterface.h>

#include <set>
#include <string>

namespace uipreview {

/**
 * Resolves every RmlUi path against a single root, the way the Phase 3 Unreal file interface
 * will, and records what was opened.
 *
 * That record is the hot reload dependency list: it names the document, every @import-ed and
 * <link>-ed stylesheet, every font and every texture, without a single line of import
 * parsing, and it cannot drift from RmlUi's own resolution rules.
 */
class RootedFileInterface : public Rml::FileInterface {
public:
	RootedFileInterface(std::string root, std::string folio_root);

	Rml::FileHandle Open(const Rml::String& path) override;
	void Close(Rml::FileHandle file) override;
	size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
	bool Seek(Rml::FileHandle file, long offset, int origin) override;
	size_t Tell(Rml::FileHandle file) override;

	/** Absolute paths opened since the last ClearOpened, lowercased and forward slashed. */
	const std::set<std::string>& Opened() const { return opened; }
	void ClearOpened() { opened.clear(); }

	/** Maps an RmlUi path onto the filesystem without opening it. */
	std::string Resolve(const std::string& path) const;

	/**
	 * Opens with a short retry. Editors save by writing a temporary file and renaming over
	 * the target, so for a few milliseconds after a change notification the file is often
	 * locked or zero length. Without this, every other save looks like a parse error.
	 */
	Rml::FileHandle OpenWithRetry(const std::string& path, int timeout_ms = 500);

private:
	std::string root;
	/** Where Folio's own shipped stylesheets live, reached through /folio/. */
	std::string folio_root;
	std::set<std::string> opened;
};

} // namespace uipreview

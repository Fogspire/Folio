#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace uipreview {

/**
 * The record of art that markup asked for and that does not exist yet.
 *
 * This is what lets screens be built, navigated and animated before any art is produced:
 * an unresolved name renders as an obvious placeholder instead of failing, and accumulates
 * here so art can be made in batches rather than per screen.
 */
class MissingAssets {
public:
	struct Entry {
		std::string name;
		/** Largest size the asset was drawn at, which is the size an artist needs. */
		int width = 0;
		int height = 0;
		std::set<std::string> documents;
		std::string first_seen;
		std::string last_seen;
	};

	void SetDocument(std::string document) { current_document = std::move(document); }

	/** A sprite name that no loaded @spritesheet declares. */
	void RecordSprite(const std::string& name);
	/** A file or spritesheet source that could not be opened. */
	void RecordFileReference(const std::string& path);
	/** Widens the recorded size once the placeholder has actually been drawn. */
	void RecordSize(const std::string& name, int width, int height);

	bool Empty() const { return sprites.empty() && files.empty(); }
	const std::map<std::string, Entry>& Sprites() const { return sprites; }
	const std::map<std::string, Entry>& Files() const { return files; }

	/**
	 * Merges into the marker block of an existing file, preserving anything outside it.
	 * Rerunning with no changes rewrites the file byte for byte, so it never produces a
	 * noise commit.
	 */
	bool WriteMarkdown(const std::string& path, std::string& out_error) const;

private:
	std::map<std::string, Entry> sprites;
	std::map<std::string, Entry> files;
	std::string current_document;
};

/**
 * Builds the placeholder image: a magenta and black checkerboard with the name stamped
 * across it, tiled so it stays readable however the decorator stretches it.
 * Returns RGBA, premultiplied (every pixel is fully opaque, so the two are the same here).
 */
std::vector<unsigned char> MakePlaceholderPixels(const std::string& name, int size);

} // namespace uipreview

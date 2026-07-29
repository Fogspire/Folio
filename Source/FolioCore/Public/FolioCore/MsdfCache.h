#pragma once

#include "FolioCore/MsdfTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Folio
{
	/// Where generated fields are kept between runs.
	///
	/// Generating one is expensive - a few milliseconds, most of it resolving the outline's
	/// overlaps - so a screen touching two hundred glyphs pays most of a second the first time it
	/// appears. That cost only has to be paid once per machine.
	///
	/// A seam rather than direct file access because FolioCore has no platform layer: the preview
	/// tool writes next to the content root, and Unreal writes under Saved and has to work inside
	/// a pak. RmlUi's file interface cannot be used, as it only reads.
	class IMsdfCacheStore
	{
	public:
		virtual ~IMsdfCacheStore() = default;

		/// Returns false when nothing is stored under this key, which is the normal cold case.
		virtual bool Read(const std::string& Key, std::vector<unsigned char>& OutData) = 0;
		virtual void Write(const std::string& Key, const std::vector<unsigned char>& Data) = 0;
	};

	/// One glyph as it comes out of the generator, before it is packed.
	///
	/// Positions are deliberately absent: the atlas is shared and packs on demand, so where a
	/// glyph sits is decided at load time and would be wrong the moment another face was loaded
	/// in a different order.
	struct FCachedGlyph
	{
		std::uint32_t GlyphIndex = 0;
		float AdvanceEm = 0.0f;
		float PlaneLeft = 0.0f;
		float PlaneBottom = 0.0f;
		float PlaneRight = 0.0f;
		float PlaneTop = 0.0f;
		std::int32_t Width = 0;
		std::int32_t Height = 0;
		bool bEmpty = false;
		std::vector<unsigned char> Rgba;
	};

	/// Bumped whenever the generator changes shape enough that old fields are wrong. A cache from
	/// a previous version is discarded rather than migrated.
	inline constexpr std::uint32_t MsdfCacheVersion = 1;

	std::vector<unsigned char> SerializeGlyphCache(const std::vector<FCachedGlyph>& Glyphs,
		const FMsdfAtlasParams& Params);

	/// Returns false if the blob is truncated, from another version, or was generated with
	/// different atlas parameters.
	bool DeserializeGlyphCache(const std::vector<unsigned char>& Data, const FMsdfAtlasParams& Params,
		std::vector<FCachedGlyph>& OutGlyphs);

	/// Identifies one face instance: the file's contents, which face within it, the variation
	/// coordinates and the field parameters. Contents rather than path, so moving or renaming a
	/// font reuses the cache and editing one does not.
	std::string MakeGlyphCacheKey(const std::vector<unsigned char>& FontData, int FaceIndex,
		const FVariationCoords& Coords, const FMsdfAtlasParams& Params);
}

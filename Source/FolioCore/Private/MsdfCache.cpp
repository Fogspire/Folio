#include "FolioCore/MsdfCache.h"

#include <cstring>

namespace Folio
{
	namespace
	{
		constexpr char CacheMagic[4] = { 'F', 'M', 'S', 'D' };

		template <typename T>
		void Append(std::vector<unsigned char>& Out, const T& Value)
		{
			const auto* Bytes = reinterpret_cast<const unsigned char*>(&Value);
			Out.insert(Out.end(), Bytes, Bytes + sizeof(T));
		}

		template <typename T>
		bool Take(const std::vector<unsigned char>& Data, std::size_t& Cursor, T& OutValue)
		{
			if (Cursor + sizeof(T) > Data.size())
			{
				return false;
			}
			std::memcpy(&OutValue, Data.data() + Cursor, sizeof(T));
			Cursor += sizeof(T);
			return true;
		}

		/// FNV-1a. Not a cryptographic hash, and does not need to be: a collision would reuse one
		/// font's fields for another, and the inputs are a handful of files chosen by the game.
		std::uint64_t HashBytes(const unsigned char* Data, std::size_t Size, std::uint64_t Seed = 0xcbf29ce484222325ull)
		{
			std::uint64_t Hash = Seed;
			for (std::size_t Index = 0; Index < Size; ++Index)
			{
				Hash ^= Data[Index];
				Hash *= 0x100000001b3ull;
			}
			return Hash;
		}
	}

	std::vector<unsigned char> SerializeGlyphCache(const std::vector<FCachedGlyph>& Glyphs,
		const FMsdfAtlasParams& Params)
	{
		std::vector<unsigned char> Out;

		Out.insert(Out.end(), CacheMagic, CacheMagic + 4);
		Append(Out, MsdfCacheVersion);
		Append(Out, std::int32_t(Params.PxPerEm));
		Append(Out, std::int32_t(Params.PxRange));
		Append(Out, std::uint32_t(Glyphs.size()));

		for (const FCachedGlyph& Glyph : Glyphs)
		{
			Append(Out, Glyph.GlyphIndex);
			Append(Out, Glyph.AdvanceEm);
			Append(Out, Glyph.PlaneLeft);
			Append(Out, Glyph.PlaneBottom);
			Append(Out, Glyph.PlaneRight);
			Append(Out, Glyph.PlaneTop);
			Append(Out, Glyph.Width);
			Append(Out, Glyph.Height);
			Append(Out, std::uint8_t(Glyph.bEmpty ? 1 : 0));
			Append(Out, std::uint32_t(Glyph.Rgba.size()));
			Out.insert(Out.end(), Glyph.Rgba.begin(), Glyph.Rgba.end());
		}

		return Out;
	}

	bool DeserializeGlyphCache(const std::vector<unsigned char>& Data, const FMsdfAtlasParams& Params,
		std::vector<FCachedGlyph>& OutGlyphs)
	{
		std::size_t Cursor = 0;
		if (Data.size() < 4 || std::memcmp(Data.data(), CacheMagic, 4) != 0)
		{
			return false;
		}
		Cursor = 4;

		std::uint32_t Version = 0;
		std::int32_t PxPerEm = 0, PxRange = 0;
		std::uint32_t Count = 0;
		if (!Take(Data, Cursor, Version) || !Take(Data, Cursor, PxPerEm)
			|| !Take(Data, Cursor, PxRange) || !Take(Data, Cursor, Count))
		{
			return false;
		}

		// The parameters are in the key as well, so a mismatch here means the key collided or the
		// file was tampered with. Either way the fields would be the wrong size.
		if (Version != MsdfCacheVersion || PxPerEm != Params.PxPerEm || PxRange != Params.PxRange)
		{
			return false;
		}

		std::vector<FCachedGlyph> Glyphs;
		Glyphs.reserve(Count);

		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FCachedGlyph Glyph;
			std::uint8_t bEmpty = 0;
			std::uint32_t ByteCount = 0;

			if (!Take(Data, Cursor, Glyph.GlyphIndex) || !Take(Data, Cursor, Glyph.AdvanceEm)
				|| !Take(Data, Cursor, Glyph.PlaneLeft) || !Take(Data, Cursor, Glyph.PlaneBottom)
				|| !Take(Data, Cursor, Glyph.PlaneRight) || !Take(Data, Cursor, Glyph.PlaneTop)
				|| !Take(Data, Cursor, Glyph.Width) || !Take(Data, Cursor, Glyph.Height)
				|| !Take(Data, Cursor, bEmpty) || !Take(Data, Cursor, ByteCount))
			{
				return false;
			}

			if (Cursor + ByteCount > Data.size())
			{
				return false;
			}

			Glyph.bEmpty = bEmpty != 0;
			Glyph.Rgba.assign(Data.begin() + std::ptrdiff_t(Cursor), Data.begin() + std::ptrdiff_t(Cursor + ByteCount));
			Cursor += ByteCount;

			Glyphs.push_back(std::move(Glyph));
		}

		OutGlyphs = std::move(Glyphs);
		return true;
	}

	std::string MakeGlyphCacheKey(const std::vector<unsigned char>& FontData, int FaceIndex,
		const FVariationCoords& Coords, const FMsdfAtlasParams& Params)
	{
		std::uint64_t Hash = HashBytes(FontData.data(), FontData.size());

		const auto Mix = [&Hash](const void* Data, std::size_t Size)
		{
			Hash = HashBytes(static_cast<const unsigned char*>(Data), Size, Hash);
		};

		Mix(&FaceIndex, sizeof(FaceIndex));
		Mix(&Params.PxPerEm, sizeof(Params.PxPerEm));
		Mix(&Params.PxRange, sizeof(Params.PxRange));
		Mix(&MsdfCacheVersion, sizeof(MsdfCacheVersion));

		for (const FVariationAxisValue& Axis : Coords)
		{
			Mix(Axis.Tag, 4);
			// Quantized to hundredths, matching how instances are keyed, so a value that rounds to
			// the same instance also finds the same cache entry.
			const std::int32_t Value = std::int32_t(Axis.Value * 100.0f + (Axis.Value < 0.0f ? -0.5f : 0.5f));
			Mix(&Value, sizeof(Value));
		}

		char Text[17];
		for (int Index = 0; Index < 16; ++Index)
		{
			Text[15 - Index] = "0123456789abcdef"[(Hash >> (Index * 4)) & 0xF];
		}
		Text[16] = '\0';

		return std::string(Text);
	}
}

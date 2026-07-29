#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Folio
{
	/// Handle into an IMsdfTextureSink. The value is the renderer's own Rml::TextureHandle, so it
	/// can be handed to RmlUi through CallbackTextureInterface::SetTextureHandle unchanged.
	using FMsdfHandle = std::uint64_t;

	inline constexpr FMsdfHandle InvalidMsdfHandle = 0;

	struct FMsdfPoint
	{
		int X = 0;
		int Y = 0;
	};

	struct FMsdfRect
	{
		int X = 0;
		int Y = 0;
		int Width = 0;
		int Height = 0;
	};

	/// Fields are generated once per face at PxPerEm and reused at every font size, so these two
	/// numbers fix both the memory cost and the reach of every font effect: a distance beyond
	/// PxRange/2 either side of the edge is clamped, and an outline wider than that stops growing.
	struct FMsdfAtlasParams
	{
		int PxPerEm = 48;
		int PxRange = 12;

		friend bool operator==(const FMsdfAtlasParams& A, const FMsdfAtlasParams& B)
		{
			return A.PxPerEm == B.PxPerEm && A.PxRange == B.PxRange;
		}
		friend bool operator!=(const FMsdfAtlasParams& A, const FMsdfAtlasParams& B) { return !(A == B); }

		/// The furthest an effect may reach, in em.
		///
		/// Not simply half the field's reach. Past PxRange/2 the field is saturated: one texel
		/// there says "at least this far away" and so does one a whole em away, and they are the
		/// same number. An outline whose edge lands exactly on that value therefore has no outside
		/// - every texel beyond it reads as sitting on the edge, at half coverage, filling the
		/// glyph's whole quad with a rectangle of ink. A texel of margin leaves the edge somewhere
		/// the field can still describe, and gives the antialiasing ramp room to reach zero.
		double MaxEffectEm() const
		{
			const double UsableTexels = 0.5 * double(PxRange) - EffectMarginTexels;
			return UsableTexels > 0.0 ? UsableTexels / double(PxPerEm) : 0.0;
		}

		/// Texels of field held back from the effect cap, for the ramp to fade out in.
		static constexpr double EffectMarginTexels = 1.0;
	};

	/// Selects the branch taken by FolioMsdfShade. Fill reads the median of the three MSDF
	/// channels for sharp corners; the rest read the true signed distance in alpha, which is
	/// smooth enough to dilate and blur.
	enum class EMsdfEffect : std::uint8_t
	{
		Fill = 0,
		Outline = 1,
		Glow = 2,
	};

	struct FMsdfEffectParams
	{
		EMsdfEffect Effect = EMsdfEffect::Fill;
		/// Distance to move the threshold, in em. Positive dilates.
		float OffsetEm = 0.0f;
		/// Width of the smoothstep band, in em. Zero gives a hard edge.
		float SoftnessEm = 0.0f;

		friend bool operator==(const FMsdfEffectParams& A, const FMsdfEffectParams& B)
		{
			return A.Effect == B.Effect && A.OffsetEm == B.OffsetEm && A.SoftnessEm == B.SoftnessEm;
		}
		friend bool operator!=(const FMsdfEffectParams& A, const FMsdfEffectParams& B) { return !(A == B); }
	};

	/// One glyph or vector sprite in an atlas.
	///
	/// Plane bounds are in em relative to the origin (baseline for glyphs), so a quad is built by
	/// scaling them by the font size. Atlas bounds are in texels on the named page. The two differ
	/// by the field padding, which is why they are stored separately rather than derived.
	struct FGlyphBox
	{
		float PlaneLeft = 0.0f;
		float PlaneBottom = 0.0f;
		float PlaneRight = 0.0f;
		float PlaneTop = 0.0f;

		float AtlasLeft = 0.0f;
		float AtlasBottom = 0.0f;
		float AtlasRight = 0.0f;
		float AtlasTop = 0.0f;

		/// Horizontal advance in em, from the unhinted outline so that it scales linearly.
		float AdvanceEm = 0.0f;

		std::uint16_t Page = 0;
		/// True for a glyph with no outline, such as a space. It still advances.
		bool bEmpty = false;
	};

	/// One variable-font axis setting. Tag is the four-character OpenType tag, e.g. "wght".
	struct FVariationAxisValue
	{
		char Tag[5] = { 0, 0, 0, 0, 0 };
		float Value = 0.0f;

		friend bool operator==(const FVariationAxisValue& A, const FVariationAxisValue& B)
		{
			return A.Value == B.Value && std::string(A.Tag) == std::string(B.Tag);
		}
	};

	/// The quantized axis values identifying one atlas. Sorted by tag so two logically equal
	/// coordinate sets hash the same.
	using FVariationCoords = std::vector<FVariationAxisValue>;
}

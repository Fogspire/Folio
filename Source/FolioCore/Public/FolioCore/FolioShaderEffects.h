#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Folio
{
	/**
	 * The effects RCSS cannot otherwise reach, rasterised rather than shaded.
	 *
	 * RmlUi's `shader` decorator hands an arbitrary string to CompileShader, which is the seam a
	 * backend uses to run a fragment shader of its own. Slate has no such seam through
	 * MakeCustomVerts, and none of these needs one: each is a pure function of position over the
	 * element's box, so evaluating it once into a small texture gives the same picture with no
	 * shader plumbing and no per-renderer divergence.
	 *
	 * Additive blending comes free with it. RmlUi composites premultiplied, `src + dst * (1 - a)`,
	 * so a colour written with zero alpha adds to what is under it. No blend state to change, and
	 * it works identically in both renderers.
	 */
	struct FShaderEffect
	{
		enum class EKind : std::uint8_t
		{
			None,
			/// A flat wash that adds rather than covers. Hit flashes, charge glows, heat.
			Additive,
			/// Horizontal lines. A CRT, a hologram, a damaged readout.
			Scanlines,
			/// A band travelling across the box. Cooldowns, sheen on a button, a scan.
			Sweep,
		};

		EKind Kind = EKind::None;

		/// Straight, not premultiplied. Rasterising applies the alpha.
		float R = 1.0f;
		float G = 1.0f;
		float B = 1.0f;
		float A = 1.0f;

		/// Adds to what is underneath instead of covering it.
		bool bAdditive = false;

		/// Scanlines: the repeat in fractions of the box height, and how much of each is lit.
		float Period = 0.05f;
		float Thickness = 0.5f;

		/// Sweep: where the band sits across the box, how wide it is, and its lean. All in
		/// fractions of the box, angle in degrees.
		float Position = 0.5f;
		float Width = 0.15f;
		float Angle = 0.0f;

		bool IsValid() const { return Kind != EKind::None; }
	};

	/**
	 * Reads the string from `decorator: shader("...")`.
	 *
	 * Whitespace separated, name first: `scanlines #00ff88ff 0.05 0.5`, `additive #ff4020`,
	 * `sweep #ffffff 0.5 0.15 20`. Returns false for anything it does not recognise, so an
	 * unknown name can fall through to whatever else the renderer supports.
	 */
	FOLIOCORE_API bool ParseShaderEffect(const std::string& Value, FShaderEffect& OutEffect);

	/**
	 * Evaluates the effect over a box into premultiplied RGBA.
	 *
	 * The texture is stretched over the element, so the size is a resolution rather than the
	 * element's own: these are smooth or periodic, and a modest texture is indistinguishable from
	 * evaluating per pixel. Scanlines are the exception and want a height that lands the period on
	 * whole texels, which SuggestSize works out.
	 */
	FOLIOCORE_API void RasterizeShaderEffect(const FShaderEffect& Effect, int Width, int Height,
		std::vector<unsigned char>& OutRgba);

	/// A texture size that renders this effect faithfully over a box of the given pixel size.
	FOLIOCORE_API void SuggestShaderEffectSize(const FShaderEffect& Effect, int BoxWidth, int BoxHeight,
		int& OutWidth, int& OutHeight);
}

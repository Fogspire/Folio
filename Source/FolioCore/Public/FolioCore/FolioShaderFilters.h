#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <functional>
#include <string>
#include <vector>

namespace Folio
{
	/** One argument of `filter: shader("name ...")`, resolved against the element it is on. */
	struct FShaderFilterArg
	{
		enum class EKind : unsigned char
		{
			Number,
			/// A length, resolved to the document's pixels.
			Length,
			Colour,
			Word,
		};

		EKind Kind = EKind::Number;

		/// Number or Length.
		float Value = 0.0f;

		/// Straight alpha, 0 to 1.
		float Colour[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		std::string Word;
	};

	/** How far a filter draws past the element's box, in the document's pixels. */
	using FShaderFilterReach = std::function<float(const std::vector<FShaderFilterArg>& Args)>;

	/**
	 * Says how far a named shader filter reaches beyond its element.
	 *
	 * The layer an element is drawn into is grown by this much, so a glow is not cut off at the box.
	 * Without one a filter reaches three times its largest length argument, which is what a gaussian
	 * radius given as a sigma needs.
	 */
	FOLIOCORE_API void SetShaderFilterReach(const std::string& Name, FShaderFilterReach Reach);
	FOLIOCORE_API void ClearShaderFilterReach(const std::string& Name);
	FOLIOCORE_API float ShaderFilterReach(const std::string& Name, const std::vector<FShaderFilterArg>& Args);

	/**
	 * Arguments as one string, which is how they cross RmlUi's filter dictionary to a render interface.
	 *
	 * Tokens are space separated, each a kind letter and its value: `L12.5`, `N2`, `C1,0.5,0,1`, `Wsoft`.
	 */
	FOLIOCORE_API std::string EncodeShaderFilterArgs(const std::vector<FShaderFilterArg>& Args);
	FOLIOCORE_API bool DecodeShaderFilterArgs(const std::string& Encoded, std::vector<FShaderFilterArg>& OutArgs);
}

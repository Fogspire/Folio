#pragma once

#include "FolioCore/MsdfTypes.h"

#include <string>

namespace Folio
{
	/// What a `<name>.msdf` file beside a `<name>.msdf.png` atlas says about it.
	///
	/// Deliberately a few `key=value` lines rather than JSON: both renderers have to read this,
	/// and the Unreal side has no JSON dependency. Unknown keys are ignored so the format can
	/// gain fields without breaking an older reader.
	struct FMsdfSidecar
	{
		int Version = 0;
		FMsdfAtlasParams Params;

		bool IsValid() const { return Version > 0 && Params.PxPerEm > 0 && Params.PxRange > 0; }
	};

	inline constexpr int MsdfSidecarVersion = 1;

	/// Parses sidecar text. Returns false if a required key is missing or malformed, leaving
	/// OutSidecar untouched. Callers treat failure as "this is an ordinary raster atlas".
	bool ParseMsdfSidecar(const std::string& Text, FMsdfSidecar& OutSidecar);

	std::string WriteMsdfSidecar(const FMsdfSidecar& Sidecar);

	/// Given "ui/art/vectors.msdf.png", returns "ui/art/vectors.msdf". Empty if the path does not
	/// carry the suffix that marks an atlas as a distance field.
	std::string MsdfSidecarPathFor(const std::string& TexturePath);
}

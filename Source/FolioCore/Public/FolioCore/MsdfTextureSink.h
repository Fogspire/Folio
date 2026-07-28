#pragma once

#include "FolioCore/MsdfTypes.h"

namespace Folio
{
	/// How the MSDF core reaches a renderer, and the only thing it knows about one.
	///
	/// Handles are the renderer's own Rml::TextureHandle values, so the font engine can pass one
	/// straight to CallbackTextureInterface::SetTextureHandle and the render interface recognises
	/// it again in DrawGeometry. That is what selects the MSDF shader without RmlUi needing to
	/// know the difference, and without patching RmlUi.
	///
	/// Pages are RGBA8 and must not be sRGB: the channels are distances, not colour.
	class IMsdfTextureSink
	{
	public:
		virtual ~IMsdfTextureSink() = default;

		/// Allocates a blank page. Returns InvalidMsdfHandle on failure.
		virtual FMsdfHandle CreateAtlasPage(FMsdfPoint Size, const FMsdfAtlasParams& Params) = 0;

		/// Uploads one glyph's field into an existing page. Rgba is Region.Height rows of
		/// Region.Width premultiplied RGBA8 texels, Stride bytes apart.
		///
		/// Pages are never repacked, so a region written once keeps its coordinates for the
		/// lifetime of the process and geometry RmlUi generated earlier never holds stale UVs.
		virtual void UploadAtlasRegion(FMsdfHandle Page, FMsdfRect Region, const unsigned char* Rgba, int Stride) = 0;

		/// Returns a second handle onto the same page carrying different effect parameters, so a
		/// font effect costs one extra mesh rather than a second copy of the atlas. Releasing an
		/// alias must not free the page.
		virtual FMsdfHandle CreateEffectAlias(FMsdfHandle Page, const FMsdfEffectParams& Effect) = 0;

		virtual void ReleaseAtlasHandle(FMsdfHandle Handle) = 0;
	};
}

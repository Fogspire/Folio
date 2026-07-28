#pragma once

#include "FolioCore/MsdfTextureSink.h"

#include <string>
#include <vector>

namespace msdfbake
{
	/// Keeps atlas pages in memory so they can be written to disk.
	///
	/// The renderers implement the same interface against a GPU texture. This one exists so that
	/// the baking tool drives the identical packing and generation code, rather than a second
	/// implementation that could drift from what ships.
	class BakeSink final : public Folio::IMsdfTextureSink
	{
	public:
		struct Page
		{
			int Width = 0;
			int Height = 0;
			std::vector<unsigned char> Rgba;
		};

		Folio::FMsdfHandle CreateAtlasPage(Folio::FMsdfPoint Size, const Folio::FMsdfAtlasParams& Params) override;
		void UploadAtlasRegion(Folio::FMsdfHandle Page, Folio::FMsdfRect Region, const unsigned char* Rgba, int Stride) override;
		Folio::FMsdfHandle CreateEffectAlias(Folio::FMsdfHandle Page, const Folio::FMsdfEffectParams& Effect) override;
		void ReleaseAtlasHandle(Folio::FMsdfHandle Handle) override;

		const std::vector<Page>& GetPages() const { return Pages; }

		/// Writes page Index as a PNG. Returns an error message, or empty on success.
		std::string WritePng(int Index, const std::string& Path) const;

	private:
		std::vector<Page> Pages;
	};
}

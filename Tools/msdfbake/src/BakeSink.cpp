#include "BakeSink.h"

#include <lodepng.h>

#include <cstring>

namespace msdfbake
{
	using Folio::FMsdfHandle;
	using Folio::InvalidMsdfHandle;

	FMsdfHandle BakeSink::CreateAtlasPage(Folio::FMsdfPoint Size, const Folio::FMsdfAtlasParams&)
	{
		if (Size.X <= 0 || Size.Y <= 0)
		{
			return InvalidMsdfHandle;
		}

		Page NewPage;
		NewPage.Width = Size.X;
		NewPage.Height = Size.Y;
		NewPage.Rgba.assign(std::size_t(Size.X) * std::size_t(Size.Y) * 4, 0);
		Pages.push_back(std::move(NewPage));

		// Handles are one-based so that zero stays available as the invalid value.
		return FMsdfHandle(Pages.size());
	}

	void BakeSink::UploadAtlasRegion(FMsdfHandle Handle, Folio::FMsdfRect Region,
		const unsigned char* Rgba, int Stride)
	{
		if (Handle == InvalidMsdfHandle || Handle > Pages.size() || Rgba == nullptr)
		{
			return;
		}

		Page& Target = Pages[std::size_t(Handle) - 1];
		for (int Row = 0; Row < Region.Height; ++Row)
		{
			const int DestY = Region.Y + Row;
			if (DestY < 0 || DestY >= Target.Height)
			{
				continue;
			}

			unsigned char* Dest = &Target.Rgba[(std::size_t(DestY) * std::size_t(Target.Width)
				+ std::size_t(Region.X)) * 4];
			std::memcpy(Dest, Rgba + std::size_t(Row) * std::size_t(Stride), std::size_t(Region.Width) * 4);
		}
	}

	FMsdfHandle BakeSink::CreateEffectAlias(FMsdfHandle Page, const Folio::FMsdfEffectParams&)
	{
		// Effects are a shading concern. Baking writes the field itself, which every effect reads.
		return Page;
	}

	void BakeSink::ReleaseAtlasHandle(FMsdfHandle)
	{
		// Pages live until the tool exits.
	}

	std::string BakeSink::WritePng(int Index, const std::string& Path) const
	{
		if (Index < 0 || Index >= int(Pages.size()))
		{
			return "no such page";
		}

		const Page& Source = Pages[std::size_t(Index)];
		const unsigned Error = lodepng::encode(Path, Source.Rgba,
			unsigned(Source.Width), unsigned(Source.Height));

		return Error != 0 ? lodepng_error_text(Error) : std::string();
	}
}

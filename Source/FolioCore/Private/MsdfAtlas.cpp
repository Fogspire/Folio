#include "MsdfAtlas.h"

#include "FolioCore/FolioTrace.h"

#include <RmlUi/Core/RenderManager.h>

namespace Folio
{
	namespace
	{
		/// Empty texels between packed fields. Bilinear sampling reaches half a texel past the
		/// quad edge, and a glyph's field is meaningful right up to its own edge, so without a
		/// gutter one glyph's distances bleed into its neighbour's outline.
		constexpr int GutterTexels = 1;
	}

	FMsdfAtlas::FMsdfAtlas(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams, int InPageSize)
		: Sink(InSink)
		, Params(InParams)
		, PageSize(InPageSize)
	{
	}

	FMsdfAtlas::~FMsdfAtlas()
	{
		for (const FPage& Page : Pages)
		{
			if (Page.Handle != InvalidMsdfHandle)
			{
				Sink.ReleaseAtlasHandle(Page.Handle);
			}
		}
	}

	FMsdfHandle FMsdfAtlas::GetPageHandle(int Page) const
	{
		if (Page < 0 || Page >= int(Pages.size()))
		{
			return InvalidMsdfHandle;
		}
		return Pages[std::size_t(Page)].Handle;
	}

	bool FMsdfAtlas::AddPage()
	{
		FOLIO_TRACE_SCOPE("Folio/CreateAtlasPage");

		const FMsdfHandle Handle = Sink.CreateAtlasPage(FMsdfPoint{ PageSize, PageSize }, Params);
		if (Handle == InvalidMsdfHandle)
		{
			return false;
		}

		FPage Page;
		Page.Handle = Handle;
		Page.Skyline.push_back(FSkylineNode{ 0, 0, PageSize });
		Pages.push_back(std::move(Page));

		FOLIO_TRACE_COUNTER_SET("Folio/AtlasPages", std::int64_t(Pages.size()));
		FOLIO_TRACE_MEMORY_SET("Folio/AtlasMemory", std::int64_t(GetByteSize()));
		return true;
	}

	bool FMsdfAtlas::FindPlacement(const FPage& Page, int Width, int Height, int& OutX, int& OutY,
		int& OutNodeIndex) const
	{
		int BestY = PageSize + 1;
		int BestX = 0;
		int BestNode = -1;

		for (std::size_t Index = 0; Index < Page.Skyline.size(); ++Index)
		{
			const int X = Page.Skyline[Index].X;
			if (X + Width > PageSize)
			{
				break;
			}

			// The rectangle rests on the highest run it spans, so walk forward until its width is
			// covered and take the maximum.
			int Y = Page.Skyline[Index].Y;
			int Remaining = Width;
			for (std::size_t Span = Index; Span < Page.Skyline.size() && Remaining > 0; ++Span)
			{
				if (Page.Skyline[Span].Y > Y)
				{
					Y = Page.Skyline[Span].Y;
				}
				Remaining -= Page.Skyline[Span].Width;
			}

			if (Remaining > 0 || Y + Height > PageSize)
			{
				continue;
			}

			if (Y < BestY || (Y == BestY && X < BestX))
			{
				BestY = Y;
				BestX = X;
				BestNode = int(Index);
			}
		}

		if (BestNode < 0)
		{
			return false;
		}

		OutX = BestX;
		OutY = BestY;
		OutNodeIndex = BestNode;
		return true;
	}

	void FMsdfAtlas::Occupy(FPage& Page, int NodeIndex, int X, int Y, int Width, int Height)
	{
		const FSkylineNode Raised{ X, Y + Height, Width };
		Page.Skyline.insert(Page.Skyline.begin() + NodeIndex, Raised);

		// Trim or drop whatever the new run covers.
		for (std::size_t Index = std::size_t(NodeIndex) + 1; Index < Page.Skyline.size();)
		{
			FSkylineNode& Node = Page.Skyline[Index];
			const int Overlap = (Raised.X + Raised.Width) - Node.X;
			if (Overlap <= 0)
			{
				break;
			}

			if (Overlap >= Node.Width)
			{
				Page.Skyline.erase(Page.Skyline.begin() + std::ptrdiff_t(Index));
				continue;
			}

			Node.X += Overlap;
			Node.Width -= Overlap;
			break;
		}

		// Merge runs that ended up at the same height, or the list grows without bound.
		for (std::size_t Index = 1; Index < Page.Skyline.size();)
		{
			if (Page.Skyline[Index - 1].Y == Page.Skyline[Index].Y)
			{
				Page.Skyline[Index - 1].Width += Page.Skyline[Index].Width;
				Page.Skyline.erase(Page.Skyline.begin() + std::ptrdiff_t(Index));
				continue;
			}
			++Index;
		}
	}

	bool FMsdfAtlas::Add(const FMsdfGlyphBitmap& Bitmap, float AdvanceEm, FGlyphBox& OutBox)
	{
		FOLIO_TRACE_SCOPE("Folio/PackGlyph");

		OutBox = FGlyphBox();
		OutBox.AdvanceEm = AdvanceEm;

		if (Bitmap.bEmpty || Bitmap.Width <= 0 || Bitmap.Height <= 0)
		{
			OutBox.bEmpty = true;
			++PackedGlyphs;
			return true;
		}

		const int NeededWidth = Bitmap.Width + GutterTexels;
		const int NeededHeight = Bitmap.Height + GutterTexels;
		if (NeededWidth > PageSize || NeededHeight > PageSize)
		{
			return false;
		}

		int X = 0, Y = 0, NodeIndex = 0;
		int PageIndex = -1;

		for (std::size_t Index = 0; Index < Pages.size(); ++Index)
		{
			if (FindPlacement(Pages[Index], NeededWidth, NeededHeight, X, Y, NodeIndex))
			{
				PageIndex = int(Index);
				break;
			}
		}

		if (PageIndex < 0)
		{
			if (!AddPage() || !FindPlacement(Pages.back(), NeededWidth, NeededHeight, X, Y, NodeIndex))
			{
				return false;
			}
			PageIndex = int(Pages.size()) - 1;
		}

		FPage& Page = Pages[std::size_t(PageIndex)];
		Occupy(Page, NodeIndex, X, Y, NeededWidth, NeededHeight);

		{
			FOLIO_TRACE_SCOPE("Folio/UploadGlyph");
			Sink.UploadAtlasRegion(Page.Handle, FMsdfRect{ X, Y, Bitmap.Width, Bitmap.Height },
				Bitmap.Rgba.data(), Bitmap.Width * 4);
		}

		// Both rects are inset by half a texel. A quad that reaches the outermost texel centre
		// samples exactly on the boundary, where bilinear filtering mixes in the gutter beside it
		// and pulls the field towards "far outside", eroding the glyph's edge. Trimming half a
		// texel keeps every sample inside the field, and the plane bounds shrink to match so the
		// glyph still lands where its metrics say it should.
		const float HalfTexelEm = 0.5f / float(Params.PxPerEm);

		OutBox.PlaneLeft = Bitmap.PlaneLeft + HalfTexelEm;
		OutBox.PlaneBottom = Bitmap.PlaneBottom + HalfTexelEm;
		OutBox.PlaneRight = Bitmap.PlaneRight - HalfTexelEm;
		OutBox.PlaneTop = Bitmap.PlaneTop - HalfTexelEm;

		OutBox.AtlasLeft = float(X) + 0.5f;
		OutBox.AtlasTop = float(Y) + 0.5f;
		OutBox.AtlasRight = float(X + Bitmap.Width) - 0.5f;
		OutBox.AtlasBottom = float(Y + Bitmap.Height) - 0.5f;

		OutBox.Page = static_cast<std::uint16_t>(PageIndex);
		++PackedGlyphs;
		FOLIO_TRACE_COUNTER_SET("Folio/AtlasGlyphs", std::int64_t(PackedGlyphs));
		return true;
	}

	std::size_t FMsdfAtlas::GetByteSize() const
	{
		return Pages.size() * std::size_t(PageSize) * std::size_t(PageSize) * 4;
	}

	Rml::Texture FMsdfAtlasSet::GetPageTexture(Rml::RenderManager& RenderManager, int Page)
	{
		if (Page < 0 || Page >= Atlas.GetPageCount())
		{
			return Rml::Texture();
		}

		// Built here rather than when the page is allocated, so ReleaseTextures can drop them
		// without losing the pages themselves.
		while (int(PageTextures.size()) <= Page)
		{
			const int Index = int(PageTextures.size());
			const FMsdfHandle Handle = Atlas.GetPageHandle(Index);
			const int Size = Atlas.GetPageSize();

			// The handle is the renderer's own texture handle, so handing it straight to RmlUi is
			// what lets the render interface recognise this as a field rather than an image.
			PageTextures.push_back(std::make_unique<Rml::CallbackTextureSource>(
				[Handle, Size](const Rml::CallbackTextureInterface& TextureInterface) -> bool
				{
					TextureInterface.SetTextureHandle(Rml::TextureHandle(Handle), Rml::Vector2i(Size, Size));
					return true;
				}));
		}

		return PageTextures[std::size_t(Page)]->GetTexture(RenderManager);
	}

	Rml::Texture FMsdfAtlasSet::GetEffectTexture(Rml::RenderManager& RenderManager, const int Page,
		const FMsdfEffectParams& Effect)
	{
		if (Page < 0 || Page >= Atlas.GetPageCount())
		{
			return Rml::Texture();
		}

		const FEffectKey Key { Page, Effect };

		for (const FEffectTexture& Existing : EffectTextures)
		{
			if (Existing.Key == Key)
			{
				return Existing.Texture->GetTexture(RenderManager);
			}
		}

		// One alias per distinct effect, not per string. A screen styled from a handful of classes
		// produces a handful of these no matter how much text it draws.
		const FMsdfHandle Alias = Atlas.GetSink().CreateEffectAlias(Atlas.GetPageHandle(Page), Effect);
		if (Alias == InvalidMsdfHandle)
		{
			return GetPageTexture(RenderManager, Page);
		}

		const int Size = Atlas.GetPageSize();

		EffectTextures.push_back({ Key, std::make_unique<Rml::CallbackTextureSource>(
			[Alias, Size](const Rml::CallbackTextureInterface& TextureInterface) -> bool
			{
				TextureInterface.SetTextureHandle(Rml::TextureHandle(Alias), Rml::Vector2i(Size, Size));
				return true;
			}) });

		return EffectTextures.back().Texture->GetTexture(RenderManager);
	}
}

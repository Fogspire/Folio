#pragma once

#include "FolioCore/MsdfTextureSink.h"
#include "FolioCore/MsdfTypes.h"

#include "MsdfGlyphGenerator.h"

#include <vector>

namespace Folio
{
	/// A set of distance-field pages that grows by appending.
	///
	/// Pages are never repacked and entries never move. RmlUi caches the geometry it generates for
	/// a string and only rebuilds it when the face's version changes, so a repack would leave
	/// live meshes pointing at the wrong texels. Appending is also why a full page is abandoned
	/// rather than compacted: a little wasted area costs less than the bookkeeping to avoid it.
	class FMsdfAtlas
	{
	public:
		FMsdfAtlas(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams, int InPageSize);
		~FMsdfAtlas();

		FMsdfAtlas(const FMsdfAtlas&) = delete;
		FMsdfAtlas& operator=(const FMsdfAtlas&) = delete;

		/// Packs and uploads one field. An empty bitmap consumes no space but still yields a box,
		/// so a space behaves like any other glyph to the caller.
		bool Add(const FMsdfGlyphBitmap& Bitmap, float AdvanceEm, FGlyphBox& OutBox);

		FMsdfHandle GetPageHandle(int Page) const;
		int GetPageCount() const { return int(Pages.size()); }
		int GetPageSize() const { return PageSize; }
		const FMsdfAtlasParams& GetParams() const { return Params; }

	private:
		/// One horizontal run of the skyline: everything at or right of X, up to the next node, is
		/// occupied to height Y.
		struct FSkylineNode
		{
			int X = 0;
			int Y = 0;
			int Width = 0;
		};

		struct FPage
		{
			FMsdfHandle Handle = InvalidMsdfHandle;
			std::vector<FSkylineNode> Skyline;
		};

		/// Lowest position the rectangle fits at, or false if it does not fit on this page.
		bool FindPlacement(const FPage& Page, int Width, int Height, int& OutX, int& OutY, int& OutNodeIndex) const;
		void Occupy(FPage& Page, int NodeIndex, int X, int Y, int Width, int Height);
		bool AddPage();

		IMsdfTextureSink& Sink;
		FMsdfAtlasParams Params;
		int PageSize = 1024;
		std::vector<FPage> Pages;
	};
}

#pragma once

#include "FolioCore/MsdfTextureSink.h"
#include "FolioCore/MsdfTypes.h"

#include "MsdfGlyphGenerator.h"

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/Texture.h>

#include <memory>
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
		/// The renderer, so an effect can be aliased onto a page without going back through the set.
		IMsdfTextureSink& GetSink() const { return Sink; }
		int GetPageCount() const { return int(Pages.size()); }
		int GetPageSize() const { return PageSize; }
		const FMsdfAtlasParams& GetParams() const { return Params; }

		int GetGlyphCount() const { return PackedGlyphs; }
		/// Bytes the pages occupy on the GPU, which is what the memory counter reports.
		std::size_t GetByteSize() const;

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
		int PackedGlyphs = 0;
		std::vector<FPage> Pages;
	};

	/// The atlas every face shares, plus the RmlUi textures for its pages.
	///
	/// Pages are shared rather than owned per face because a page costs its full area the moment
	/// it exists - a 1024 square RGBA page is four megabytes whether it holds twelve glyphs or a
	/// thousand. Giving each face its own meant a weight sweep across one family cost tens of
	/// megabytes to store a few hundred glyphs. Nothing about a field says which face produced
	/// it, so they pack together happily.
	class FMsdfAtlasSet
	{
	public:
		FMsdfAtlasSet(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams, int InPageSize)
			: Atlas(InSink, InParams, InPageSize)
		{
		}

		FMsdfAtlas& GetAtlas() { return Atlas; }
		const FMsdfAtlas& GetAtlas() const { return Atlas; }

		Rml::Texture GetPageTexture(Rml::RenderManager& RenderManager, int Page);

		/// The same page, resolved with an effect rather than as a plain fill.
		///
		/// Nothing is generated or copied: the alias is the same field with different shader
		/// parameters, which is the whole reason effects are free here. A glow is the true-distance
		/// channel thresholded further out, not a second atlas.
		Rml::Texture GetEffectTexture(Rml::RenderManager& RenderManager, int Page,
			const FMsdfEffectParams& Effect);

		/// Drops the RmlUi-side textures while keeping the pages and the fields in them. RmlUi
		/// checks at shutdown that no callback texture outlived it, so these cannot simply be left
		/// to the destructor.
		void ReleaseTextures()
		{
			PageTextures.clear();
			EffectTextures.clear();
		}

	private:
		struct FEffectKey
		{
			int Page = 0;
			FMsdfEffectParams Effect;

			bool operator==(const FEffectKey& Other) const
			{
				return Page == Other.Page && Effect == Other.Effect;
			}
		};

		struct FEffectTexture
		{
			FEffectKey Key;
			std::unique_ptr<Rml::CallbackTextureSource> Texture;
		};

		std::vector<FEffectTexture> EffectTextures;

		FMsdfAtlas Atlas;
		/// One per page. A CallbackTextureSource rather than a CallbackTexture because a process
		/// can have more than one render manager, and each needs its own resource.
		std::vector<std::unique_ptr<Rml::CallbackTextureSource>> PageTextures;
	};
}

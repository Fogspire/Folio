#include "FolioCore/FolioFontEngine.h"

#include "FolioFontFace.h"

#include <algorithm>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/StringUtilities.h>

namespace Folio
{
	struct FFolioFontEngine::FFaceEntry
	{
		Rml::String Family;
		Rml::Style::FontStyle Style = Rml::Style::FontStyle::Normal;
		Rml::Style::FontWeight Weight = Rml::Style::FontWeight::Normal;
		bool bFallback = false;
		std::unique_ptr<FFolioFontFace> Face;
	};

	/// A face resolved at one size. Handles are cheap: the atlas they read from is shared, so
	/// asking for the same family at ten sizes costs ten of these and one atlas.
	struct FFolioFontEngine::FHandle
	{
		FFaceEntry* Entry = nullptr;
		int Size = 0;
		Rml::FontMetrics Metrics{};
	};

	struct FFolioFontEngine::FImpl
	{
		IMsdfTextureSink& Sink;
		FMsdfAtlasParams Params;
		int PageSize = 1024;

		FT_Library Library = nullptr;
		std::vector<std::unique_ptr<FFaceEntry>> Faces;
		std::vector<std::unique_ptr<FHandle>> Handles;

		Rml::FontMetrics EmptyMetrics{};

		explicit FImpl(IMsdfTextureSink& InSink) : Sink(InSink) {}
	};

	namespace
	{
		/// FontWeight::Auto means "whatever the author did not say", which CSS resolves to normal.
		int ResolveWeight(Rml::Style::FontWeight Weight)
		{
			const int Value = int(Weight);
			return Value == 0 ? 400 : Value;
		}
	}

	FFolioFontEngine::FFolioFontEngine(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams, int InPageSize)
		: Impl(std::make_unique<FImpl>(InSink))
	{
		Impl->Params = InParams;
		Impl->PageSize = InPageSize;

		if (FT_Init_FreeType(&Impl->Library) != 0)
		{
			Impl->Library = nullptr;
			Rml::Log::Message(Rml::Log::LT_ERROR, "FolioMsdf: FreeType would not start; no text can be rendered.");
		}
	}

	FFolioFontEngine::~FFolioFontEngine()
	{
		// Faces hold FT_Face objects, so they must go before the library that owns them.
		Impl->Handles.clear();
		Impl->Faces.clear();

		if (Impl->Library != nullptr)
		{
			FT_Done_FreeType(Impl->Library);
		}
	}

	bool FFolioFontEngine::RegisterFace(std::unique_ptr<FFolioFontFace> Face, const Rml::String& Family,
		Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, bool bFallbackFace)
	{
		auto Entry = std::make_unique<FFaceEntry>();
		Entry->Family = Rml::StringUtilities::ToLower(Family);
		Entry->Style = Style;
		Entry->Weight = Weight;
		Entry->bFallback = bFallbackFace;
		Entry->Face = std::move(Face);

		Impl->Faces.push_back(std::move(Entry));
		return true;
	}

	bool FFolioFontEngine::LoadFontFace(const Rml::String& FileName, int FaceIndex, bool bFallbackFace,
		Rml::Style::FontWeight Weight)
	{
		if (Impl->Library == nullptr)
		{
			return false;
		}

		// Read through RmlUi's file interface rather than letting FreeType open the path itself.
		// That is what makes mounts like /folio/ and /ui/fonts/ resolve, and in Unreal it is the
		// difference between working in a packaged build and not: the default reads with fopen,
		// which cannot see inside a pak.
		std::vector<unsigned char> Bytes;
		if (Rml::FileInterface* Files = Rml::GetFileInterface())
		{
			if (const Rml::FileHandle File = Files->Open(FileName))
			{
				Files->Seek(File, 0, SEEK_END);
				const std::size_t Length = Files->Tell(File);
				Files->Seek(File, 0, SEEK_SET);

				Bytes.resize(Length);
				if (Length == 0 || Files->Read(Bytes.data(), Length, File) != Length)
				{
					Bytes.clear();
				}
				Files->Close(File);
			}
		}

		if (Bytes.empty())
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "FolioMsdf: cannot read font face %s", FileName.c_str());
			return false;
		}

		auto Face = std::make_unique<FFolioFontFace>(Impl->Library, Impl->Sink, Impl->Params, Impl->PageSize);
		if (!Face->LoadFromMemory(std::move(Bytes), FaceIndex))
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "FolioMsdf: %s is not a scalable font face", FileName.c_str());
			return false;
		}

		// The family and style come from the file when the caller did not name them, which is the
		// path RmlUi takes for Rml::LoadFontFace.
		const Rml::String Family = Face->GetFtFace()->family_name != nullptr
			? Rml::String(Face->GetFtFace()->family_name) : Rml::String("unnamed");
		const Rml::Style::FontStyle Style = (Face->GetFtFace()->style_flags & FT_STYLE_FLAG_ITALIC) != 0
			? Rml::Style::FontStyle::Italic : Rml::Style::FontStyle::Normal;
		const Rml::Style::FontWeight ResolvedWeight = Weight == Rml::Style::FontWeight::Auto
			? ((Face->GetFtFace()->style_flags & FT_STYLE_FLAG_BOLD) != 0
				? Rml::Style::FontWeight::Bold : Rml::Style::FontWeight::Normal)
			: Weight;

		return RegisterFace(std::move(Face), Family, Style, ResolvedWeight, bFallbackFace);
	}

	bool FFolioFontEngine::LoadFontFace(Rml::Span<const Rml::byte> Data, int FaceIndex, const Rml::String& Family,
		Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, bool bFallbackFace)
	{
		if (Impl->Library == nullptr)
		{
			return false;
		}

		// This overload is not only for callers with their own bytes: Rml::Debugger registers its
		// embedded face through it as 'rmlui-debugger-font'. Without it the debugger opens with no
		// text at all.
		auto Face = std::make_unique<FFolioFontFace>(Impl->Library, Impl->Sink, Impl->Params, Impl->PageSize);
		if (!Face->LoadFromMemory(std::vector<unsigned char>(Data.begin(), Data.end()), FaceIndex))
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "FolioMsdf: cannot load the in-memory font face %s", Family.c_str());
			return false;
		}

		return RegisterFace(std::move(Face), Family, Style, Weight, bFallbackFace);
	}

	FFolioFontEngine::FFaceEntry* FFolioFontEngine::FindFace(const Rml::String& Family,
		Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight)
	{
		const Rml::String Wanted = Rml::StringUtilities::ToLower(Family);
		const int WantedWeight = ResolveWeight(Weight);

		FFaceEntry* Best = nullptr;
		int BestScore = -1;

		for (const auto& Entry : Impl->Faces)
		{
			if (Entry->Family != Wanted)
			{
				continue;
			}

			// Style is categorical and weight is a distance, so a matching style always beats a
			// closer weight: an upright face at 700 is a better answer for bold than an italic at
			// 400.
			int Score = Entry->Style == Style ? 2000 : 0;
			Score -= std::abs(ResolveWeight(Entry->Weight) - WantedWeight);

			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Entry.get();
			}
		}

		if (Best == nullptr)
		{
			for (const auto& Entry : Impl->Faces)
			{
				if (Entry->bFallback)
				{
					return Entry.get();
				}
			}
		}

		return Best;
	}

	Rml::FontFaceHandle FFolioFontEngine::GetFontFaceHandle(const Rml::String& Family,
		Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, int Size)
	{
		FFaceEntry* Entry = FindFace(Family, Style, Weight);
		if (Entry == nullptr || Size <= 0)
		{
			return 0;
		}

		for (std::size_t Index = 0; Index < Impl->Handles.size(); ++Index)
		{
			const FHandle& Existing = *Impl->Handles[Index];
			if (Existing.Entry == Entry && Existing.Size == Size)
			{
				return Rml::FontFaceHandle(Index + 1);
			}
		}

		auto Handle = std::make_unique<FHandle>();
		Handle->Entry = Entry;
		Handle->Size = Size;

		const FFolioFontFace& Face = *Entry->Face;
		const float Scale = float(Size);

		Handle->Metrics.size = Size;
		Handle->Metrics.ascent = Face.GetAscentEm() * Scale;
		Handle->Metrics.descent = Face.GetDescentEm() * Scale;
		Handle->Metrics.line_spacing = Face.GetLineSpacingEm() * Scale;
		Handle->Metrics.x_height = Face.GetXHeightEm() * Scale;
		Handle->Metrics.underline_position = Face.GetUnderlinePositionEm() * Scale;
		Handle->Metrics.underline_thickness = std::max(Face.GetUnderlineThicknessEm() * Scale, 1.0f);
		Handle->Metrics.has_ellipsis = Face.HasEllipsis();

		Impl->Handles.push_back(std::move(Handle));

		// Zero is RmlUi's invalid handle, so indices are stored one-based.
		return Rml::FontFaceHandle(Impl->Handles.size());
	}

	FFolioFontEngine::FHandle* FFolioFontEngine::ResolveHandle(Rml::FontFaceHandle Handle)
	{
		if (Handle == 0 || Handle > Impl->Handles.size())
		{
			return nullptr;
		}
		return Impl->Handles[std::size_t(Handle) - 1].get();
	}

	Rml::FontEffectsHandle FFolioFontEngine::PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&)
	{
		// Effects are resolved in the shader from the field's true-distance channel rather than
		// baked into extra atlas layers. Until that lands, text draws unadorned.
		return 0;
	}

	const Rml::FontMetrics& FFolioFontEngine::GetFontMetrics(Rml::FontFaceHandle Handle)
	{
		const FHandle* Resolved = ResolveHandle(Handle);
		return Resolved != nullptr ? Resolved->Metrics : Impl->EmptyMetrics;
	}

	int FFolioFontEngine::GetStringWidth(Rml::FontFaceHandle Handle, Rml::StringView String,
		const Rml::TextShapingContext& TextShapingContext, Rml::Character PriorCharacter)
	{
		FHandle* Resolved = ResolveHandle(Handle);
		if (Resolved == nullptr)
		{
			return 0;
		}

		FFolioFontFace& Face = *Resolved->Entry->Face;
		const float Scale = float(Resolved->Size);
		const bool bKerning = TextShapingContext.font_kerning != Rml::Style::FontKerning::None;

		// Advances accumulate as whole pixels, matching RmlUi's own engine. Layout, caret
		// positions and hit testing are all integer, so accumulating in float here would put this
		// engine half a pixel out from everything that measures text.
		int Width = 0;
		unsigned int PriorGlyph = PriorCharacter != Rml::Character::Null
			? Face.GetGlyphIndex(char32_t(PriorCharacter)) : 0;

		for (auto It = Rml::StringIteratorU8(String); It; ++It)
		{
			const unsigned int GlyphIndex = Face.GetGlyphIndex(char32_t(*It));
			const FGlyphBox* Box = Face.GetOrAppendGlyph(GlyphIndex);
			if (Box == nullptr)
			{
				continue;
			}

			if (bKerning && PriorGlyph != 0)
			{
				Width += int(Face.GetKerningEm(PriorGlyph, GlyphIndex) * Scale);
			}

			Width += int(Box->AdvanceEm * Scale);
			Width += int(TextShapingContext.letter_spacing);
			PriorGlyph = GlyphIndex;
		}

		return Rml::Math::Max(Width, 0);
	}

	int FFolioFontEngine::GenerateString(Rml::RenderManager& RenderManager, Rml::FontFaceHandle FaceHandle,
		Rml::FontEffectsHandle, Rml::StringView String, Rml::Vector2f Position,
		Rml::ColourbPremultiplied Colour, float, const Rml::TextShapingContext& TextShapingContext,
		Rml::TexturedMeshList& MeshList)
	{
		FHandle* Resolved = ResolveHandle(FaceHandle);
		if (Resolved == nullptr)
		{
			return 0;
		}

		FFolioFontFace& Face = *Resolved->Entry->Face;
		const float Scale = float(Resolved->Size);
		const bool bKerning = TextShapingContext.font_kerning != Rml::Style::FontKerning::None;

		// One mesh per atlas page. A string almost always sits on a single page, so this is
		// usually one draw; it only splits when a face has outgrown a page.
		MeshList.clear();
		std::vector<int> MeshForPage;

		int Width = 0;
		unsigned int PriorGlyph = 0;

		for (auto It = Rml::StringIteratorU8(String); It; ++It)
		{
			const unsigned int GlyphIndex = Face.GetGlyphIndex(char32_t(*It));
			const FGlyphBox* Box = Face.GetOrAppendGlyph(GlyphIndex);
			if (Box == nullptr)
			{
				continue;
			}

			if (bKerning && PriorGlyph != 0)
			{
				Width += int(Face.GetKerningEm(PriorGlyph, GlyphIndex) * Scale);
			}

			if (!Box->bEmpty)
			{
				if (std::size_t(Box->Page) >= MeshForPage.size())
				{
					MeshForPage.resize(std::size_t(Box->Page) + 1, -1);
				}

				if (MeshForPage[Box->Page] < 0)
				{
					MeshForPage[Box->Page] = int(MeshList.size());
					MeshList.emplace_back();
					MeshList.back().texture = Face.GetPageTexture(RenderManager, Box->Page);
				}

				Rml::Mesh& Mesh = MeshList[std::size_t(MeshForPage[Box->Page])].mesh;

				// Plane bounds are y-up from the baseline; RmlUi's y runs down the screen.
				const Rml::Vector2f Origin(
					Position.x + float(Width) + Box->PlaneLeft * Scale,
					Position.y - Box->PlaneTop * Scale);
				const Rml::Vector2f Dimensions(
					(Box->PlaneRight - Box->PlaneLeft) * Scale,
					(Box->PlaneTop - Box->PlaneBottom) * Scale);

				const float Texels = float(Impl->PageSize);
				const Rml::Vector2f TopLeftUv(Box->AtlasLeft / Texels, Box->AtlasTop / Texels);
				const Rml::Vector2f BottomRightUv(Box->AtlasRight / Texels, Box->AtlasBottom / Texels);

				Rml::MeshUtilities::GenerateQuad(Mesh, Origin.Round(), Dimensions, Colour, TopLeftUv, BottomRightUv);
			}

			Width += int(Box->AdvanceEm * Scale);
			Width += int(TextShapingContext.letter_spacing);
			PriorGlyph = GlyphIndex;
		}

		return Rml::Math::Max(Width, 0);
	}

	int FFolioFontEngine::GetVersion(Rml::FontFaceHandle Handle)
	{
		const FHandle* Resolved = ResolveHandle(Handle);
		return Resolved != nullptr ? Resolved->Entry->Face->GetVersion() : 0;
	}

	void FFolioFontEngine::Shutdown()
	{
		ReleaseFontResources();
	}

	void FFolioFontEngine::ReleaseFontResources()
	{
		// Handles are what RmlUi holds, and it treats every one as invalid after this call. The
		// faces and their atlases stay: regenerating fields is expensive and they are the thing
		// worth keeping.
		Impl->Handles.clear();

		// The textures cannot stay, though. Rml::Shutdown calls this and then asserts that no
		// callback texture survived it, so holding on to them reports a leak and warns about
		// memory corruption on the way out.
		for (const auto& Entry : Impl->Faces)
		{
			Entry->Face->ReleaseTextures();
		}
	}

	std::string FFolioFontEngine::GetReport() const
	{
		std::string Report = "engine=FolioMsdf faces=" + std::to_string(Impl->Faces.size())
			+ " handles=" + std::to_string(Impl->Handles.size());

		for (const auto& Entry : Impl->Faces)
		{
			const FFolioFontFace& Face = *Entry->Face;
			Report += "\n  " + Entry->Family
				+ " style=" + (Entry->Style == Rml::Style::FontStyle::Italic ? "italic" : "normal")
				+ " weight=" + std::to_string(ResolveWeight(Entry->Weight))
				+ " variable=" + (Face.IsVariable() ? "yes" : "no")
				+ " glyphs=" + std::to_string(Face.GetGlyphCount())
				+ " pages=" + std::to_string(Face.GetPageCount())
				+ " pxem=" + std::to_string(Face.GetParams().PxPerEm)
				+ " pxrange=" + std::to_string(Face.GetParams().PxRange);
		}

		return Report;
	}
}

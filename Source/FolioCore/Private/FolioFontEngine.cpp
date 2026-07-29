#include "FolioCore/FolioFontEngine.h"

#include "FolioCore/FolioFontEffects.h"
#include "FolioCore/FolioHost.h"
#include "FolioCore/FolioTrace.h"

#include "FolioFontFace.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Math.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/StringUtilities.h>

namespace Folio
{
	/// One registered font file, and every instance cut from it so far.
	///
	/// A static file has exactly one instance. A variable file has one per distinct coordinate
	/// set, because two weights are two sets of outlines and therefore two atlases - which is why
	/// the coordinates are quantized before they get here.
	struct FFolioFontEngine::FFaceEntry
	{
		Rml::String Family;
		Rml::Style::FontStyle Style = Rml::Style::FontStyle::Normal;
		Rml::Style::FontWeight Weight = Rml::Style::FontWeight::Normal;
		bool bFallback = false;

		FFontData Data;
		int FaceIndex = 0;
		/// Axes fixed at registration, for anything RCSS cannot say.
		FVariationCoords PinnedAxes;
		/// Read from the first instance, so the mapping knows what the file actually supports.
		std::vector<FVariationAxis> Axes;

		std::vector<std::pair<Rml::String, std::unique_ptr<FFolioFontFace>>> Instances;

		bool HasAxis(const char* Tag) const
		{
			for (const FVariationAxis& Axis : Axes)
			{
				if (std::strcmp(Axis.Tag, Tag) == 0)
				{
					return true;
				}
			}
			return false;
		}

		const FVariationAxis* FindAxis(const char* Tag) const
		{
			for (const FVariationAxis& Axis : Axes)
			{
				if (std::strcmp(Axis.Tag, Tag) == 0)
				{
					return &Axis;
				}
			}
			return nullptr;
		}
	};

	/// One extra pass over the string, drawn before the text itself.
	struct FFolioFontEngine::FEffectLayer
	{
		FMsdfEffectParams Params;
		Rml::ColourbPremultiplied Colour;
		float OffsetX = 0.0f;
		float OffsetY = 0.0f;
	};

	/// A face instance resolved at one size. Handles are cheap: the atlas they read from is
	/// shared, so asking for the same instance at ten sizes costs ten of these and one atlas.
	struct FFolioFontEngine::FHandle
	{
		FFaceEntry* Entry = nullptr;
		FFolioFontFace* Face = nullptr;
		int Size = 0;
		Rml::FontMetrics Metrics{};
	};

	struct FFolioFontEngine::FImpl
	{
		IMsdfTextureSink& Sink;
		FMsdfAtlasParams Params;
		int PageSize = 1024;

		FT_Library Library = nullptr;
		/// One atlas for every face. See FMsdfAtlasSet for why they are not per face.
		std::unique_ptr<FMsdfAtlasSet> Atlas;
		std::vector<std::unique_ptr<FFaceEntry>> Faces;
		std::vector<std::unique_ptr<FHandle>> Handles;

		IMsdfCacheStore* CacheStore = nullptr;

		/// Effect sets, indexed by handle. Entry zero is "no effects", so a handle of 0 keeps
		/// meaning plain text and RmlUi's default needs no special case.
		std::vector<std::vector<FEffectLayer>> EffectSets;

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

		/// Once each, not once per string. A stylesheet that asks for too wide an effect would
		/// otherwise repeat the same line for every text element on the screen, every frame.
		void WarnEffectClamped(float MaxEm)
		{
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				LogWarning(Format("a font effect reaches further than the field describes and was "
					"clamped to %.3f em. Raise pxrange to go wider, at a quadratic cost in atlas "
					"area", double(MaxEm)));
			}
		}

		void WarnForeignEffect()
		{
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				LogWarning("a font effect that is not one of Folio's was skipped. Effects here are "
					"resolved from the distance field rather than baked into glyph textures, so an "
					"effect written against RmlUi's own interface has nothing to draw");
			}
		}

		/// Advances have to land on whole pixels, because layout, caret positions and hit testing
		/// are all integer. Truncating loses up to a pixel per glyph and always in the same
		/// direction, so the error compounds along a run and characters end up overlapping - most
		/// visibly at small sizes, where a pixel is a large share of an advance. Rounding keeps
		/// the error centred on zero.
		int RoundToPixels(float Value)
		{
			return int(Value + (Value < 0.0f ? -0.5f : 0.5f));
		}

		/// Weights are quantized before they become an atlas. font-weight is continuous over
		/// [1,1000], and animating it would otherwise generate a field set per frame.
		constexpr float WeightQuantum = 50.0f;

		/// Optical size buckets. opsz is meant to track the size text is set at, but honouring
		/// every size would defeat a size-independent atlas, so the axis range is divided into a
		/// few steps.
		constexpr int OpticalSizeBuckets = 4;

		float Quantize(float Value, float Quantum)
		{
			return std::round(Value / Quantum) * Quantum;
		}

		float Clamp(float Value, float Low, float High)
		{
			return Value < Low ? Low : (Value > High ? High : Value);
		}

		FVariationAxisValue MakeAxis(const char* Tag, float Value)
		{
			FVariationAxisValue Out;
			std::strncpy(Out.Tag, Tag, 4);
			Out.Tag[4] = '\0';
			Out.Value = Value;
			return Out;
		}

		/// A stable name for a coordinate set, used to find an instance that already exists.
		Rml::String CoordsKey(const FVariationCoords& Coords)
		{
			FVariationCoords Sorted = Coords;
			std::sort(Sorted.begin(), Sorted.end(),
				[](const FVariationAxisValue& A, const FVariationAxisValue& B)
				{ return std::strcmp(A.Tag, B.Tag) < 0; });

			Rml::String Key;
			for (const FVariationAxisValue& Axis : Sorted)
			{
				Key += Rml::String(Axis.Tag) + "=" + std::to_string(int(std::round(Axis.Value * 100.0f))) + ";";
			}
			return Key;
		}
	}

	FFolioFontEngine::FFolioFontEngine(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams, int InPageSize)
		: Impl(std::make_unique<FImpl>(InSink))
	{
		Impl->Params = InParams;
		Impl->PageSize = InPageSize;
		Impl->Atlas = std::make_unique<FMsdfAtlasSet>(InSink, InParams, InPageSize);

		if (FT_Init_FreeType(&Impl->Library) != 0)
		{
			Impl->Library = nullptr;
			LogError("FolioMsdf: FreeType would not start, so no text can be rendered");
		}
	}

	bool FFolioFontEngine::IsValid() const
	{
		return Impl->Library != nullptr;
	}

	void FFolioFontEngine::SetCacheStore(IMsdfCacheStore* Store)
	{
		Impl->CacheStore = Store;
	}

	FFolioFontEngine::~FFolioFontEngine()
	{
		// Faces hold FT_Face objects, so they must go before the library that owns them.
		Impl->Handles.clear();
		Impl->Faces.clear();
		Impl->Atlas.reset();

		if (Impl->Library != nullptr)
		{
			FT_Done_FreeType(Impl->Library);
		}
	}

	/// Builds the instance for a coordinate set, or returns the one already built.
	FFolioFontFace* FFolioFontEngine::GetInstance(FFaceEntry& Entry, const FVariationCoords& Coords)
	{
		const Rml::String Key = CoordsKey(Coords);
		for (const auto& Instance : Entry.Instances)
		{
			if (Instance.first == Key)
			{
				return Instance.second.get();
			}
		}

		auto Face = std::make_unique<FFolioFontFace>(Impl->Library, *Impl->Atlas);
		if (!Face->Load(Entry.Data, Entry.FaceIndex, Coords))
		{
			return nullptr;
		}

		if (Impl->CacheStore != nullptr && Entry.Data)
		{
			Face->SetCache(Impl->CacheStore,
				MakeGlyphCacheKey(*Entry.Data, Entry.FaceIndex, Coords, Impl->Params));
		}

		FFolioFontFace* Raw = Face.get();
		Entry.Instances.emplace_back(Key, std::move(Face));
		FOLIO_TRACE_COUNTER_ADD("Folio/FaceInstances", 1);
		return Raw;
	}

	/// Maps what RCSS can say onto what the file declares.
	///
	/// GetFontFaceHandle is given family, style, weight and size and nothing else, so those are
	/// the only things an axis can be driven from. font-weight maps to wght because they share a
	/// scale, and font-size maps to opsz because that is what an optical size axis is for.
	FVariationCoords FFolioFontEngine::BuildCoords(const FFaceEntry& Entry, Rml::Style::FontWeight Weight,
		int Size) const
	{
		FVariationCoords Coords = Entry.PinnedAxes;
		if (Entry.Axes.empty())
		{
			return Coords;
		}

		auto AlreadyPinned = [&Coords](const char* Tag)
		{
			for (const FVariationAxisValue& Axis : Coords)
			{
				if (std::strcmp(Axis.Tag, Tag) == 0)
				{
					return true;
				}
			}
			return false;
		};

		if (const FVariationAxis* Wght = Entry.FindAxis("wght"); Wght != nullptr && !AlreadyPinned("wght"))
		{
			const float Wanted = Clamp(float(ResolveWeight(Weight)), Wght->Minimum, Wght->Maximum);
			Coords.push_back(MakeAxis("wght", Clamp(Quantize(Wanted, WeightQuantum), Wght->Minimum, Wght->Maximum)));
		}

		if (const FVariationAxis* Opsz = Entry.FindAxis("opsz"); Opsz != nullptr && !AlreadyPinned("opsz"))
		{
			const float Range = Opsz->Maximum - Opsz->Minimum;
			float Value = Clamp(float(Size), Opsz->Minimum, Opsz->Maximum);
			if (Range > 0.0f)
			{
				const float Steps = float(OpticalSizeBuckets - 1);
				Value = Opsz->Minimum + std::round((Value - Opsz->Minimum) / Range * Steps) / Steps * Range;
			}
			Coords.push_back(MakeAxis("opsz", Value));
		}

		return Coords;
	}

	bool FFolioFontEngine::RegisterFace(FFontData Data, int FaceIndex, const Rml::String& Family,
		Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, bool bFallbackFace,
		const FVariationCoords& PinnedAxes)
	{
		auto Entry = std::make_unique<FFaceEntry>();
		Entry->Family = Rml::StringUtilities::ToLower(Family);
		Entry->Style = Style;
		Entry->Weight = Weight;
		Entry->bFallback = bFallbackFace;
		Entry->Data = std::move(Data);
		Entry->FaceIndex = FaceIndex;
		Entry->PinnedAxes = PinnedAxes;

		// Building one instance up front is what reveals the axes, and it is the instance most
		// documents will ask for anyway.
		FFaceEntry* Raw = Entry.get();
		Impl->Faces.push_back(std::move(Entry));

		FFolioFontFace* First = GetInstance(*Raw, PinnedAxes);
		if (First == nullptr)
		{
			LogError(Format("FolioMsdf: could not build the first instance of family %s", Family.c_str()));
			Impl->Faces.pop_back();
			return false;
		}

		Raw->Axes = First->GetAxes();

		// A variable file's default instance is never the one asked for, because the weight and
		// size always resolve to explicit coordinates. Keeping it would leave a face and an entry
		// in every report that nothing ever draws from. It is rebuilt on demand if some axis
		// combination really does come back empty.
		if (!Raw->Axes.empty())
		{
			Raw->Instances.clear();
		}

		return true;
	}

	FFontData FFolioFontEngine::ReadFontFile(const Rml::String& FileName) const
	{
		// Through the host rather than Rml::GetFileInterface(). RmlUi is a static library, so in
		// Unreal this module has its own copy of that global and it is null: the module that
		// installed the interface set a different one. The host is also what resolves mounts and
		// what makes reading from inside a pak work.
		IFolioHost* Host = GetHost();
		if (Host == nullptr)
		{
			return nullptr;
		}

		auto Bytes = std::make_shared<std::vector<unsigned char>>();
		if (!Host->ReadFile(FileName, *Bytes) || Bytes->empty())
		{
			return nullptr;
		}

		return FFontData(Bytes);
	}

	bool FFolioFontEngine::LoadFontFace(const Rml::String& FileName, int FaceIndex, bool bFallbackFace,
		Rml::Style::FontWeight Weight)
	{
		if (Impl->Library == nullptr)
		{
			return false;
		}

		FFontData Data = ReadFontFile(FileName);
		if (!Data)
		{
			LogError(Format("FolioMsdf: cannot read font face %s", FileName.c_str()));
			return false;
		}

		// The family and style come from the file when the caller did not name them, which is the
		// path RmlUi takes for Rml::LoadFontFace. Reading them needs a face, so a throwaway one is
		// opened before the entry is registered.
		FFolioFontFace Probe(Impl->Library, *Impl->Atlas);
		if (!Probe.Load(Data, FaceIndex, {}))
		{
			LogError(Format("FolioMsdf: %s is not a scalable font face", FileName.c_str()));
			return false;
		}

		const Rml::String Family = Probe.GetFtFace()->family_name != nullptr
			? Rml::String(Probe.GetFtFace()->family_name) : Rml::String("unnamed");
		const Rml::Style::FontStyle Style = (Probe.GetFtFace()->style_flags & FT_STYLE_FLAG_ITALIC) != 0
			? Rml::Style::FontStyle::Italic : Rml::Style::FontStyle::Normal;

		// A variable file covers the whole weight range, so pinning it to the style flags' idea of
		// bold would hide every other weight behind a family that cannot reach them.
		const Rml::Style::FontWeight ResolvedWeight = Weight != Rml::Style::FontWeight::Auto
			? Weight
			: (Probe.IsVariable()
				? Rml::Style::FontWeight::Auto
				: ((Probe.GetFtFace()->style_flags & FT_STYLE_FLAG_BOLD) != 0
					? Rml::Style::FontWeight::Bold : Rml::Style::FontWeight::Normal));

		return RegisterFace(std::move(Data), FaceIndex, Family, Style, ResolvedWeight, bFallbackFace, {});
	}

	bool FFolioFontEngine::RegisterVariableFace(const Rml::String& FileName, const Rml::String& Family,
		Rml::Style::FontStyle Style, const FVariationCoords& PinnedAxes, bool bFallbackFace)
	{
		if (Impl->Library == nullptr)
		{
			return false;
		}

		FFontData Data = ReadFontFile(FileName);
		if (!Data)
		{
			LogError(Format("FolioMsdf: cannot read font face %s", FileName.c_str()));
			return false;
		}

		return RegisterFace(std::move(Data), 0, Family, Style, Rml::Style::FontWeight::Auto,
			bFallbackFace, PinnedAxes);
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
		auto Bytes = std::make_shared<std::vector<unsigned char>>(Data.begin(), Data.end());
		if (!RegisterFace(FFontData(Bytes), FaceIndex, Family, Style, Weight, bFallbackFace, {}))
		{
			LogError(Format("FolioMsdf: cannot load the in-memory font face %s", Family.c_str()));
			return false;
		}
		return true;
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

		// The weight and size choose the instance, so two weights of one variable file resolve to
		// different faces and different atlases.
		FFolioFontFace* Instance = GetInstance(*Entry, BuildCoords(*Entry, Weight, Size));
		if (Instance == nullptr)
		{
			return 0;
		}

		for (std::size_t Index = 0; Index < Impl->Handles.size(); ++Index)
		{
			const FHandle& Existing = *Impl->Handles[Index];
			if (Existing.Face == Instance && Existing.Size == Size)
			{
				return Rml::FontFaceHandle(Index + 1);
			}
		}

		auto Handle = std::make_unique<FHandle>();
		Handle->Entry = Entry;
		Handle->Face = Instance;
		Handle->Size = Size;

		const FFolioFontFace& Face = *Instance;
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

	Rml::FontEffectsHandle FFolioFontEngine::PrepareFontEffects(const Rml::FontFaceHandle FaceHandle,
		const Rml::FontEffectList& FontEffects)
	{
		const FHandle* Resolved = ResolveHandle(FaceHandle);
		if (Resolved == nullptr || FontEffects.empty())
		{
			return 0;
		}

		// Sizes are written against the font size and the field is indexed in em, so everything
		// converts once, here, rather than at every glyph.
		const float ToEm = Resolved->Size > 0 ? 1.0f / float(Resolved->Size) : 0.0f;

		std::vector<FEffectLayer> Layers;

		for (const Rml::SharedPtr<const Rml::FontEffect>& Effect : FontEffects)
		{
			const FFolioFontEffect* Folio = AsFolioFontEffect(Effect.get());
			if (Folio == nullptr)
			{
				// Somebody registered an effect of their own. It would want a baked glyph texture,
				// which this engine does not produce, so it is skipped rather than drawn as a box.
				WarnForeignEffect();
				continue;
			}

			FEffectLayer& Layer = Layers.emplace_back();
			Layer.Params = Folio->Params;

			// Effect colours are authored straight and the meshes want premultiplied, the same
			// conversion RmlUi applies to a text colour before it reaches here.
			Layer.Colour = Folio->GetColour().ToPremultiplied();
			Layer.OffsetX = Folio->OffsetX;
			Layer.OffsetY = Folio->OffsetY;

			// Widths arrive in the length units the stylesheet used, which resolve against the font
			// size, so they become em first.
			float OffsetEm = Folio->Params.OffsetEm * ToEm;
			float SoftnessEm = Folio->Params.SoftnessEm * ToEm;

			// The field only describes distance out to half its range, and the last texel of that
			// is needed for the effect's own edge to fade out in, so an effect cannot reach as far
			// as the stylesheet may ask. Clamped rather than left to saturate, which fills each
			// glyph's quad with a rectangle of half-covered ink.
			const float MaxEm = float(Impl->Params.MaxEffectEm());
			if (OffsetEm > MaxEm || SoftnessEm > MaxEm)
			{
				WarnEffectClamped(MaxEm);
				OffsetEm = std::min(OffsetEm, MaxEm);
				SoftnessEm = std::min(SoftnessEm, MaxEm);
			}

			// The shader moves a threshold, and a threshold lives in the field's own units, where
			// the whole range spans one. That range is PxRange texels wide whatever the effect cap
			// happens to be, so it is derived from the atlas rather than from the cap: tying the
			// two together would rescale every effect the moment the margin changed.
			const float RangeEm = float(Impl->Params.PxRange) / float(Impl->Params.PxPerEm);
			Layer.Params.OffsetEm = RangeEm > 0.0f ? OffsetEm / RangeEm : 0.0f;
			Layer.Params.SoftnessEm = RangeEm > 0.0f ? SoftnessEm / RangeEm : 0.0f;
		}

		if (Layers.empty())
		{
			return 0;
		}

		Impl->EffectSets.push_back(std::move(Layers));
		return Rml::FontEffectsHandle(Impl->EffectSets.size());
	}

	const Rml::FontMetrics& FFolioFontEngine::GetFontMetrics(Rml::FontFaceHandle Handle)
	{
		const FHandle* Resolved = ResolveHandle(Handle);
		return Resolved != nullptr ? Resolved->Metrics : Impl->EmptyMetrics;
	}

	int FFolioFontEngine::GetStringWidth(Rml::FontFaceHandle Handle, Rml::StringView String,
		const Rml::TextShapingContext& TextShapingContext, Rml::Character PriorCharacter)
	{
		FOLIO_TRACE_SCOPE("Folio/GetStringWidth");

		FHandle* Resolved = ResolveHandle(Handle);
		if (Resolved == nullptr)
		{
			return 0;
		}

		FFolioFontFace& Face = *Resolved->Face;
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
				Width += RoundToPixels(Face.GetKerningEm(PriorGlyph, GlyphIndex) * Scale);
			}

			Width += RoundToPixels(Box->AdvanceEm * Scale);
			Width += int(TextShapingContext.letter_spacing);
			PriorGlyph = GlyphIndex;
		}

		return Rml::Math::Max(Width, 0);
	}

	int FFolioFontEngine::GenerateString(Rml::RenderManager& RenderManager, Rml::FontFaceHandle FaceHandle,
		Rml::FontEffectsHandle EffectsHandle, Rml::StringView String, Rml::Vector2f Position,
		Rml::ColourbPremultiplied Colour, float, const Rml::TextShapingContext& TextShapingContext,
		Rml::TexturedMeshList& MeshList)
	{
		FOLIO_TRACE_SCOPE("Folio/GenerateString");

		FHandle* Resolved = ResolveHandle(FaceHandle);
		if (Resolved == nullptr)
		{
			return 0;
		}

		MeshList.clear();

		// Effects are extra passes over the same glyphs, drawn first so the text lands on top.
		// They share the atlas and differ only in which threshold the shader reads it at, so an
		// outline costs one more quad per glyph and nothing else.
		if (EffectsHandle != 0 && std::size_t(EffectsHandle) <= Impl->EffectSets.size())
		{
			for (const FEffectLayer& Layer : Impl->EffectSets[std::size_t(EffectsHandle) - 1])
			{
				AppendPass(RenderManager, *Resolved, String, Position + Rml::Vector2f(Layer.OffsetX, Layer.OffsetY),
					Layer.Colour, TextShapingContext, &Layer.Params, MeshList);
			}
		}

		return AppendPass(RenderManager, *Resolved, String, Position, Colour, TextShapingContext,
			nullptr, MeshList);
	}

	int FFolioFontEngine::AppendPass(Rml::RenderManager& RenderManager, FHandle& Resolved,
		Rml::StringView String, Rml::Vector2f Position, Rml::ColourbPremultiplied Colour,
		const Rml::TextShapingContext& TextShapingContext, const FMsdfEffectParams* Effect,
		Rml::TexturedMeshList& MeshList)
	{
		FFolioFontFace& Face = *Resolved.Face;
		const float Scale = float(Resolved.Size);
		const bool bKerning = TextShapingContext.font_kerning != Rml::Style::FontKerning::None;

		// One mesh per atlas page per pass. A string almost always sits on a single page, so this
		// is usually one draw; it only splits when a face has outgrown a page.
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
				Width += RoundToPixels(Face.GetKerningEm(PriorGlyph, GlyphIndex) * Scale);
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
					MeshList.back().texture = Effect != nullptr
						? Face.GetEffectTexture(RenderManager, Box->Page, *Effect)
						: Face.GetPageTexture(RenderManager, Box->Page);
				}

				Rml::Mesh& Mesh = MeshList[std::size_t(MeshForPage[Box->Page])].mesh;

				// Snap the pen, not the glyph. Rounding a composed origin quantises each glyph's
				// own bearing along with the pen, so characters meant to share a baseline land on
				// slightly different ones. It is invisible at text sizes and unmistakable once a
				// transform magnifies it.
				//
				// Plane bounds are y-up from the baseline; RmlUi's y runs down the screen.
				const float PenX = std::round(Position.x + float(Width));
				const float BaselineY = std::round(Position.y);

				const Rml::Vector2f Origin(
					PenX + Box->PlaneLeft * Scale,
					BaselineY - Box->PlaneTop * Scale);
				const Rml::Vector2f Dimensions(
					(Box->PlaneRight - Box->PlaneLeft) * Scale,
					(Box->PlaneTop - Box->PlaneBottom) * Scale);

				const float Texels = float(Impl->PageSize);
				const Rml::Vector2f TopLeftUv(Box->AtlasLeft / Texels, Box->AtlasTop / Texels);
				const Rml::Vector2f BottomRightUv(Box->AtlasRight / Texels, Box->AtlasBottom / Texels);

Rml::MeshUtilities::GenerateQuad(Mesh, Origin, Dimensions, Colour, TopLeftUv, BottomRightUv);
			}

			Width += RoundToPixels(Box->AdvanceEm * Scale);
			Width += int(TextShapingContext.letter_spacing);
			PriorGlyph = GlyphIndex;
		}

		return Rml::Math::Max(Width, 0);
	}

	int FFolioFontEngine::GetVersion(Rml::FontFaceHandle Handle)
	{
		const FHandle* Resolved = ResolveHandle(Handle);
		return Resolved != nullptr ? Resolved->Face->GetVersion() : 0;
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

		// Write the fields back before anything is torn down, so a run that generated glyphs is
		// the last one that has to.
		for (const auto& Entry : Impl->Faces)
		{
			for (const auto& Instance : Entry->Instances)
			{
				Instance.second->FlushCache();
			}
		}

		// The textures cannot stay, though. Rml::Shutdown calls this and then asserts that no
		// callback texture survived it, so holding on to them reports a leak and warns about
		// memory corruption on the way out.
		Impl->Atlas->ReleaseTextures();
	}

	std::string FFolioFontEngine::GetReport() const
	{
		const FMsdfAtlas& Atlas = Impl->Atlas->GetAtlas();
		std::string Report = "engine=FolioMsdf faces=" + std::to_string(Impl->Faces.size())
			+ " handles=" + std::to_string(Impl->Handles.size())
			+ " pages=" + std::to_string(Atlas.GetPageCount())
			+ " atlas=" + std::to_string(Atlas.GetByteSize() / (1024 * 1024)) + "MB"
			+ " glyphs=" + std::to_string(Atlas.GetGlyphCount());

		for (const auto& Entry : Impl->Faces)
		{
			for (const auto& Instance : Entry->Instances)
			{
				const FFolioFontFace& Face = *Instance.second;
				Report += "\n  " + Entry->Family
					+ " style=" + (Entry->Style == Rml::Style::FontStyle::Italic ? "italic" : "normal")
					+ " variable=" + (Face.IsVariable() ? "yes" : "no")
					+ " axes=" + (Instance.first.empty() ? Rml::String("default") : Instance.first)
					+ " glyphs=" + std::to_string(Face.GetGlyphCount())
					+ " cached=" + std::to_string(Face.GetCacheHits())
					+ " generated=" + std::to_string(Face.GetCacheMisses());
			}
		}

		return Report;
	}
}

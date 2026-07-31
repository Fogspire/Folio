#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include "FolioCore/MsdfTextureSink.h"
#include "FolioCore/MsdfTypes.h"

#include <memory>
#include <string>
#include <vector>

#include "FolioCore/MsdfCache.h"

#include <RmlUi/Core/FontEngineInterface.h>

namespace Folio
{
	class FFolioFontFace;

	/// Shared bytes of a font file. Declared in FolioFontFace.h.
	using FFontData = std::shared_ptr<const std::vector<unsigned char>>;

	/// Replaces RmlUi's FreeType font engine with one that renders from multi-channel distance
	/// fields, so text stays sharp at any size, any DPI and under any transform scale.
	///
	/// Install it with Rml::SetFontEngineInterface before Rml::Initialise. Both builds set
	/// RMLUI_FONT_ENGINE to none, which makes Initialise fail loudly if that is forgotten rather
	/// than falling back to something that silently draws nothing.
	class FOLIOCORE_API FFolioFontEngine final : public Rml::FontEngineInterface
	{
	public:
		explicit FFolioFontEngine(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams = {}, int InPageSize = 1024);

		/// Installs the field cache. Fields survive between runs, which matters because generating
		/// one costs a few milliseconds and a screen can want hundreds. Set it before loading any
		/// face; the store must outlive the engine.
		void SetCacheStore(IMsdfCacheStore* Store);

		/// False when FreeType would not start, in which case every face load fails and nothing
		/// renders. Worth checking at startup rather than inferring it from the silence.
		bool IsValid() const;
		~FFolioFontEngine() override;

		FFolioFontEngine(const FFolioFontEngine&) = delete;
		FFolioFontEngine& operator=(const FFolioFontEngine&) = delete;

		/// Rml::Shutdown calls this, not ReleaseFontResources, and then checks that no callback
		/// texture survived it.
		void Shutdown() override;

		bool LoadFontFace(const Rml::String& FileName, int FaceIndex, bool bFallbackFace,
			Rml::Style::FontWeight Weight) override;
		bool LoadFontFace(Rml::Span<const Rml::byte> Data, int FaceIndex, const Rml::String& Family,
			Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, bool bFallbackFace) override;

		Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& Family, Rml::Style::FontStyle Style,
			Rml::Style::FontWeight Weight, int Size) override;

		Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle Handle,
			const Rml::FontEffectList& FontEffects) override;

		const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle Handle) override;

		int GetStringWidth(Rml::FontFaceHandle Handle, Rml::StringView String,
			const Rml::TextShapingContext& TextShapingContext, Rml::Character PriorCharacter) override;

		int GenerateString(Rml::RenderManager& RenderManager, Rml::FontFaceHandle FaceHandle,
			Rml::FontEffectsHandle FontEffectsHandle, Rml::StringView String, Rml::Vector2f Position,
			Rml::ColourbPremultiplied Colour, float Opacity,
			const Rml::TextShapingContext& TextShapingContext, Rml::TexturedMeshList& MeshList) override;

		int GetVersion(Rml::FontFaceHandle Handle) override;

		void ReleaseFontResources() override;

		/// Registers a variable font file under a family name with some axes pinned.
		///
		/// GetFontFaceHandle only receives family, style, weight and size, so an axis RCSS cannot
		/// express has to be baked into a family: register "Inter Condensed" with wdth at 75 and
		/// markup then asks for it by name. wght and opsz need none of this - they are driven from
		/// font-weight and font-size directly.
		bool RegisterVariableFace(const Rml::String& FileName, const Rml::String& Family,
			Rml::Style::FontStyle Style, const FVariationCoords& PinnedAxes, bool bFallbackFace = false);

		/// Turns OpenType shaping on or off, which is what decides whether ligatures, GPOS
		/// kerning, contextual forms and mark attachment happen at all.
		///
		/// On by default wherever HarfBuzz is available. Off falls back to one glyph per codepoint
		/// kerned from the legacy `kern` table, which is what RmlUi's own engine does. Text will
		/// reflow when this changes, so it is a diagnostic, not a setting to animate.
		void SetComplexShaping(bool bEnabled);

		/// Whether this build has a shaper at all.
		static bool IsComplexShapingAvailable();

		/// One line naming the engine, its faces and what they have cost so far. Behind
		/// uipreview --font-report and the folio.Font.Report console command, so that "is the
		/// distance-field engine actually running" has an answer that is not a debugger.
		std::string GetReport() const;

	private:
		struct FFaceEntry;
		struct FHandle;
		struct FEffectLayer;

		FFaceEntry* FindFace(const Rml::String& Family, Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight);
		FHandle* ResolveHandle(Rml::FontFaceHandle Handle);
		FFolioFontFace* GetInstance(FFaceEntry& Entry, const FVariationCoords& Coords);

		/// One pass over a string. Effects are the same glyphs read at a different threshold, so
		/// they run through here too, with Effect naming which alias to sample.
		int AppendPass(Rml::RenderManager& RenderManager, FHandle& Resolved, Rml::StringView String,
			Rml::Vector2f Position, Rml::ColourbPremultiplied Colour,
			const Rml::TextShapingContext& TextShapingContext, const FMsdfEffectParams* Effect,
			Rml::TexturedMeshList& MeshList);
		FVariationCoords BuildCoords(const FFaceEntry& Entry, Rml::Style::FontWeight Weight, int Size) const;
		bool RegisterFace(FFontData Data, int FaceIndex, const Rml::String& Family,
			Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, bool bFallbackFace,
			const FVariationCoords& PinnedAxes);
		/// Reads a file through RmlUi's file interface, so mounts resolve and a packaged build works.
		FFontData ReadFontFile(const Rml::String& FileName) const;
		void BuildShaper();

		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}

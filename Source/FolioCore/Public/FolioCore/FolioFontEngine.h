#pragma once

#include "FolioCore/MsdfTextureSink.h"
#include "FolioCore/MsdfTypes.h"

#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core/FontEngineInterface.h>

namespace Folio
{
	class FFolioFontFace;

	/// Replaces RmlUi's FreeType font engine with one that renders from multi-channel distance
	/// fields, so text stays sharp at any size, any DPI and under any transform scale.
	///
	/// Install it with Rml::SetFontEngineInterface before Rml::Initialise. Both builds set
	/// RMLUI_FONT_ENGINE to none, which makes Initialise fail loudly if that is forgotten rather
	/// than falling back to something that silently draws nothing.
	class FFolioFontEngine final : public Rml::FontEngineInterface
	{
	public:
		explicit FFolioFontEngine(IMsdfTextureSink& InSink, const FMsdfAtlasParams& InParams = {}, int InPageSize = 1024);
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

		/// One line naming the engine, its faces and what they have cost so far. Behind
		/// uipreview --font-report and the folio.Font.Report console command, so that "is the
		/// distance-field engine actually running" has an answer that is not a debugger.
		std::string GetReport() const;

	private:
		struct FFaceEntry;
		struct FHandle;

		FFaceEntry* FindFace(const Rml::String& Family, Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight);
		FHandle* ResolveHandle(Rml::FontFaceHandle Handle);
		bool RegisterFace(std::unique_ptr<FFolioFontFace> Face, const Rml::String& Family,
			Rml::Style::FontStyle Style, Rml::Style::FontWeight Weight, bool bFallbackFace);

		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}

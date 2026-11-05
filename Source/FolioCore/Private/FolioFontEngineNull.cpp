// The font engine for targets built without FreeType, which is every dedicated server. FFolioModule
// never starts RmlUi on one, so nothing calls this; it exists so the rest of Folio links unchanged.
#if !FOLIO_WITH_FREETYPE

#include "FolioCore/FolioFontEngine.h"

#include <RmlUi/Core/StringUtilities.h>

namespace Folio
{
	struct FFolioFontEngine::FImpl
	{
	};

	FFolioFontEngine* FFolioFontEngine::Get()
	{
		return nullptr;
	}

	FFolioFontEngine::FFolioFontEngine(IMsdfTextureSink&, const FMsdfAtlasParams&, int)
	{
	}

	FFolioFontEngine::~FFolioFontEngine() = default;

	void FFolioFontEngine::SetCacheStore(IMsdfCacheStore*)
	{
	}

	bool FFolioFontEngine::IsValid() const
	{
		return false;
	}

	void FFolioFontEngine::Shutdown()
	{
	}

	bool FFolioFontEngine::LoadFontFace(const Rml::String&, int, bool, Rml::Style::FontWeight)
	{
		return false;
	}

	bool FFolioFontEngine::LoadFontFace(Rml::Span<const Rml::byte>, int, const Rml::String&,
		Rml::Style::FontStyle, Rml::Style::FontWeight, bool)
	{
		return false;
	}

	Rml::FontFaceHandle FFolioFontEngine::GetFontFaceHandle(const Rml::String&, Rml::Style::FontStyle,
		Rml::Style::FontWeight, int)
	{
		return 0;
	}

	Rml::FontEffectsHandle FFolioFontEngine::PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&)
	{
		return 0;
	}

	Rml::FontEffectsHandle FFolioFontEngine::PrepareFontEffects(Rml::FontFaceHandle, const Rml::FontEffectList&, float)
	{
		return 0;
	}

	const Rml::FontMetrics& FFolioFontEngine::GetFontMetrics(Rml::FontFaceHandle)
	{
		static const Rml::FontMetrics Empty{};
		return Empty;
	}

	int FFolioFontEngine::GetStringWidth(Rml::FontFaceHandle, Rml::StringView, const Rml::TextShapingContext&,
		Rml::Character)
	{
		return 0;
	}

	int FFolioFontEngine::GenerateString(Rml::RenderManager&, Rml::FontFaceHandle, Rml::FontEffectsHandle,
		Rml::StringView, Rml::Vector2f, Rml::ColourbPremultiplied, float, const Rml::TextShapingContext&,
		Rml::TexturedMeshList&)
	{
		return 0;
	}

	int FFolioFontEngine::GenerateStringPainted(Rml::RenderManager&, Rml::FontFaceHandle, Rml::FontEffectsHandle,
		Rml::StringView, Rml::Vector2f, Rml::ColourbPremultiplied, const Rml::TextShapingContext&,
		const IGlyphPainter&, Rml::TexturedMeshList&)
	{
		return 0;
	}

	int FFolioFontEngine::GetVersion(Rml::FontFaceHandle)
	{
		return 0;
	}

	void FFolioFontEngine::ReleaseFontResources()
	{
	}

	bool FFolioFontEngine::RegisterVariableFace(const Rml::String&, const Rml::String&, Rml::Style::FontStyle,
		const FVariationCoords&, bool)
	{
		return false;
	}

	void FFolioFontEngine::SetComplexShaping(bool)
	{
	}

	bool FFolioFontEngine::IsComplexShapingAvailable()
	{
		return false;
	}

	std::string FFolioFontEngine::GetReport() const
	{
		return "FolioMsdf: built without FreeType, no faces";
	}
}

#endif

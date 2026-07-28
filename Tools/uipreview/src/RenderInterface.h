#pragma once

#include "MsdfProgram.h"

#include "FolioCore/MsdfTextureSink.h"

#include "RmlUi_Renderer_GL3.h"

#include <map>
#include <string>
#include <vector>

namespace uipreview {

class Diagnostics;
class MissingAssets;

/**
 * Turns unresolved art into something visible and recorded, rather than something silent.
 *
 * Most decorators resolve a value as a sprite name first and fall back to treating it as a
 * filename without logging anything, so the only place a missing sprite surfaces is a failed
 * texture load. Returning a placeholder here instead of zero both suppresses RmlUi's own
 * message, letting the tool own the wording, and yields the name that failed to resolve.
 */
class PreviewRenderInterface : public RenderInterface_GL3, public Folio::IMsdfTextureSink {
public:
	PreviewRenderInterface(Diagnostics& diagnostics, MissingAssets& missing_assets);

	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	/** Field pages are ours, not GL texture names, so they must not reach the base class. */
	void ReleaseTexture(Rml::TextureHandle texture) override;

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	// Folio::IMsdfTextureSink
	Folio::FMsdfHandle CreateAtlasPage(Folio::FMsdfPoint size, const Folio::FMsdfAtlasParams& params) override;
	void UploadAtlasRegion(Folio::FMsdfHandle page, Folio::FMsdfRect region, const unsigned char* rgba, int stride) override;
	Folio::FMsdfHandle CreateEffectAlias(Folio::FMsdfHandle page, const Folio::FMsdfEffectParams& effect) override;
	void ReleaseAtlasHandle(Folio::FMsdfHandle handle) override;

	/** Drops the placeholder cache, so a reload picks up art that has since appeared. */
	void ClearPlaceholders();

	int MsdfDrawCount() const { return msdf_draw_count; }

private:
	/** A field's texture plus how it should be resolved. Aliases share a page and differ only in
	    the effect, so a font effect costs one extra mesh rather than a second atlas. */
	struct MsdfBinding {
		unsigned int texture = 0;
		Rml::Vector2f unit_range;
		Folio::FMsdfEffectParams effect;
	};

	/** Vertices kept so a geometry can be rebuilt into our own buffers if it turns out to be a
	    field. The base class owns its copy and will not share it. */
	struct ShadowGeometry {
		Rml::Vector2f size;
		std::vector<Rml::Vertex> vertices;
		std::vector<int> indices;
		MsdfGeometry msdf;
	};

	Diagnostics& diagnostics;
	MissingAssets& missing_assets;

	/** Placeholder texture handle to the name it stands in for. */
	std::map<Rml::TextureHandle, std::string> placeholder_names;
	std::map<std::string, Rml::TextureHandle> placeholder_textures;
	std::map<Rml::CompiledGeometryHandle, ShadowGeometry> shadow_geometry;

	std::map<Rml::TextureHandle, MsdfBinding> msdf_bindings;
	Rml::TextureHandle next_msdf_handle = Rml::TextureHandle(1) << 48;

	MsdfProgram msdf_program;
	bool msdf_program_ready = false;
	int msdf_draw_count = 0;
};

} // namespace uipreview

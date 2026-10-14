#pragma once

#include "MsdfProgram.h"

#include "FolioCore/FolioShaderEffects.h"
#include "FolioCore/MsdfTextureSink.h"

#include "RmlUi_Renderer_GL3.h"

#include <map>
#include <set>
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

	/** Recognises Folio's own filter names; everything else goes to the base. */
	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;

	/** Folio's own named effects; anything else is the base class's gradients. */
	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
		Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseShader(Rml::CompiledShaderHandle shader) override;
	void ReleaseFilter(Rml::CompiledFilterHandle filter) override;
	void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
		Rml::Span<const Rml::CompiledFilterHandle> filters) override;

	/** Recorded on the way past, because outside-blur has to know what it is the outside of. */
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;
	/** The viewport outside-blur inverts against. Not SetViewport: the base's is not virtual, and
	    the backend calls it through a base pointer. */
	void SetContextSize(Rml::Vector2i size) { viewport = size; }

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	// Folio::IMsdfTextureSink
	Folio::FMsdfHandle CreateAtlasPage(Folio::FMsdfPoint size, const Folio::FMsdfAtlasParams& params) override;
	void UploadAtlasRegion(Folio::FMsdfHandle page, Folio::FMsdfRect region, const unsigned char* rgba, int stride) override;
	Folio::FMsdfHandle CreateEffectAlias(Folio::FMsdfHandle page, const Folio::FMsdfEffectParams& effect) override;
	void ReleaseAtlasHandle(Folio::FMsdfHandle handle) override;

	/** Registers a baked sheet as a field page if it has a sidecar. Empty if it is ordinary art. */
	Rml::TextureHandle LoadFieldAtlas(const Rml::String& source, const std::vector<unsigned char>& pixels,
		Rml::Vector2i size);

	/** Drops the placeholder cache, so a reload picks up art that has since appeared. */
	void ClearPlaceholders();

	int MsdfDrawCount() const { return msdf_draw_count; }
	void SetMsdfSharpen(float value) { msdf_sharpen = value; }

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
	/** Three quarters of the way up the handle, which is bit 48 natively and bit 24 on wasm32. */
	Rml::TextureHandle next_msdf_handle = Rml::TextureHandle(1) << (sizeof(Rml::TextureHandle) * 6);

	/**
	 * Field pages that came from a file rather than from the font engine.
	 *
	 * A glyph atlas belongs to the font engine, which keeps writing new glyphs into it for the
	 * life of the process. A baked vector sheet belongs to RmlUi, which loaded it. Only the
	 * second may be torn down when RmlUi releases its textures.
	 */
	std::set<Rml::TextureHandle> field_atlases_from_file;

	/**
	 * Folio's outside-blur, against the ordinary blur it is drawn with.
	 *
	 * The inversion is not something a filter can express - a filter runs over a buffer, and this
	 * one is about where the result lands. So it compiles to a plain blur, and CompositeLayers
	 * composites that blur in four bands around the element instead of once over it.
	 */
	std::map<Rml::CompiledFilterHandle, Rml::CompiledFilterHandle> outside_blurs;

	/** Carried between the two composites backdrop-filter issues, so the second does the work. */
	bool pending_outside = false;

	/** Fill patterns by path, loaded once. Textured text names a file rather than a sprite: a font
	    effect is built while the stylesheet parses and has no way to reach a spritesheet. */
	std::map<std::string, unsigned int> fill_textures;

	/** The GL texture for a fill pattern path, loaded on first use. Zero if it will not load. */
	unsigned int ResolveFill(const std::string& path);

	/** A rasterised shader effect, against the texture it was baked into. */
	struct ShaderEffect {
		Folio::FShaderEffect effect;
		Rml::TextureHandle texture = {};
	};

	std::map<Rml::CompiledShaderHandle, ShaderEffect> shader_effects;
	/**
	 * Well clear of the base class's handles, which are pointers into its own shader list. Odd, and
	 * stepped by two, because on wasm32 a pointer can be any value but is never odd.
	 */
	Rml::CompiledShaderHandle next_shader_effect = (Rml::CompiledShaderHandle(1) << (sizeof(Rml::CompiledShaderHandle) * 6)) | 1;

	Rml::Rectanglei scissor = Rml::Rectanglei::MakeInvalid();
	Rml::Vector2i viewport;

	MsdfProgram msdf_program;
	bool msdf_program_ready = false;
	int msdf_draw_count = 0;
	float msdf_sharpen = 1.0f;
};

} // namespace uipreview

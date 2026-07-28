#pragma once

#include "RmlUi_Renderer_GL3.h"

#include <map>
#include <string>

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
class PreviewRenderInterface : public RenderInterface_GL3 {
public:
	PreviewRenderInterface(Diagnostics& diagnostics, MissingAssets& missing_assets);

	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	/** Drops the placeholder cache, so a reload picks up art that has since appeared. */
	void ClearPlaceholders();

private:
	Diagnostics& diagnostics;
	MissingAssets& missing_assets;

	/** Placeholder texture handle to the name it stands in for. */
	std::map<Rml::TextureHandle, std::string> placeholder_names;
	std::map<std::string, Rml::TextureHandle> placeholder_textures;
	/** Vertex bounds per compiled geometry, so a placeholder's drawn size can be recovered. */
	std::map<Rml::CompiledGeometryHandle, Rml::Vector2f> geometry_size;
};

} // namespace uipreview

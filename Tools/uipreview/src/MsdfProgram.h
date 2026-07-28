#pragma once

#include "FolioCore/MsdfTypes.h"

#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Vertex.h>

namespace uipreview {

/**
 * The distance-field program, and the buffers geometry needs to be drawn with it.
 *
 * RenderInterface_GL3 keeps its compiled geometry private, and redeclaring its layout to borrow
 * the VAO would break silently on an RmlUi bump. So MSDF geometry gets its own buffers, built the
 * first time a piece of geometry is actually drawn as a field. Text is a small share of a
 * document's vertices, so the duplication is cheap and the coupling saved is worth more.
 *
 * Everything else - scissor, stencil, blend state, the bound framebuffer - is global GL state the
 * base class already set correctly, so layers, clip masks and filters keep working untouched.
 */
class MsdfProgram {
public:
	~MsdfProgram();

	/** Compiles the program. Returns false and leaves an error in `error` if it will not build. */
	bool Initialise(std::string& error);

	bool IsValid() const { return program != 0; }

	void Bind(const Rml::Matrix4f& transform, Rml::Vector2f translation, unsigned int texture,
		Rml::Vector2f unit_range, const Folio::FMsdfEffectParams& effect, float sharpen);

	unsigned int program = 0;

private:
	int uniform_translate = -1;
	int uniform_transform = -1;
	int uniform_atlas = -1;
	int uniform_unit_range = -1;
	int uniform_threshold = -1;
	int uniform_mode = -1;
	int uniform_offset = -1;
	int uniform_softness = -1;
	int uniform_sharpen = -1;
};

/** GL buffers for one compiled geometry drawn as a field. */
struct MsdfGeometry {
	unsigned int vao = 0;
	unsigned int vbo = 0;
	unsigned int ibo = 0;
	int index_count = 0;

	void Upload(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices);
	void Release();
};

} // namespace uipreview

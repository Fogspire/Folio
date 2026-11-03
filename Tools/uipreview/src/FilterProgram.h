#pragma once

#include "FolioCore/FolioShaderFilters.h"

#include <map>
#include <string>
#include <vector>

namespace uipreview {

/**
 * The filters RmlUi's GL3 renderer does not have: Folio's glow, and preview shaders for a game's shader filters.
 *
 * Each draws from a layer texture into whatever framebuffer is bound when it is called, which is how the
 * GL3 backend's custom filter hook hands them over. A shader filter's real pass is the game's and runs in
 * the engine only; the preview runs `UI/filters/<name>.frag` in its place when the project has one.
 */
class FilterProgram {
public:
	~FilterProgram();

	/** glow and drop-shadow. Colour is premultiplied; offset and sigma are in pixels. */
	void DrawHalo(unsigned int source, int width, int height, const float colour[4], float offset_x, float offset_y,
		float sigma, float strength);

	/**
	 * A shader filter's preview program. Copies the layer through unchanged when there is none.
	 *
	 * Returns whether the program reads folio_time, so the caller can keep drawing.
	 */
	bool DrawShader(const std::string& name, const std::vector<Folio::FShaderFilterArg>& args, unsigned int source,
		int width, int height, float time, float pixel_scale);

private:
	struct Program {
		unsigned int id = 0;
		bool animated = false;
	};

	bool EnsureBuiltIns();
	bool EnsureTargets(int width, int height);
	const Program& ShaderFor(const std::string& name);
	void Blur(unsigned int texture, unsigned int framebuffer, float sigma, int width, int height);

	unsigned int vao = 0;
	unsigned int halo_program = 0;
	unsigned int blur_program = 0;
	unsigned int copy_program = 0;
	bool built_ins_failed = false;

	/** Two scratch targets of the layer's size: the halo, and the blur's other half. */
	unsigned int textures[2] = {0, 0};
	unsigned int framebuffers[2] = {0, 0};
	int target_width = 0;
	int target_height = 0;

	/** By filter name. An id of zero is a name with no preview program, kept so it is looked up once. */
	std::map<std::string, Program> shaders;
};

} // namespace uipreview

#include "Screenshot.h"

#include "RmlUi_Include_GL3.h"
#include "backend/UipreviewBackendGLFW.h"

#include <lodepng.h>

#include <cstring>
#include <filesystem>

namespace uipreview {

bool CaptureBackBuffer(int width, int height, std::vector<unsigned char>& out_rgba, std::string& out_error)
{
	if (width <= 0 || height <= 0)
	{
		out_error = "invalid capture size";
		return false;
	}

	std::vector<unsigned char> pixels((size_t)width * (size_t)height * 4);

	// Rows are tightly packed, which the default 4-byte alignment would not guarantee for
	// widths that are not a multiple of four.
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	// Whatever was rendered into, which is not the window when the requested size was larger than the
	// display. A colour attachment rather than GL_BACK, since an offscreen target has no back buffer.
	const GLuint target = Backend::GetCaptureFramebuffer();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
	glReadBuffer(target ? GL_COLOR_ATTACHMENT0 : GL_BACK);

	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

	if (const GLenum error = glGetError())
	{
		out_error = "glReadPixels failed (0x" + std::to_string(error) + ")";
		return false;
	}

	// OpenGL's origin is bottom left, PNG's is top left.
	out_rgba.resize(pixels.size());
	const size_t stride = (size_t)width * 4;
	for (int y = 0; y < height; ++y)
		std::memcpy(&out_rgba[(size_t)y * stride], &pixels[(size_t)(height - 1 - y) * stride], stride);

	return true;
}

bool WritePng(const std::string& path, const std::vector<unsigned char>& rgba, int width, int height,
	std::string& out_error, bool force_rgba)
{
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(path).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent, ec);

	unsigned error = 0;
	if (force_rgba)
	{
		lodepng::State state;
		state.info_raw.colortype = LCT_RGBA;
		state.info_raw.bitdepth = 8;
		state.info_png.color.colortype = LCT_RGBA;
		state.info_png.color.bitdepth = 8;
		state.encoder.auto_convert = 0;

		std::vector<unsigned char> encoded;
		error = lodepng::encode(encoded, rgba, (unsigned)width, (unsigned)height, state);
		if (!error)
			error = lodepng::save_file(encoded, path);
	}
	else
	{
		error = lodepng::encode(path, rgba, (unsigned)width, (unsigned)height);
	}

	if (error)
	{
		out_error = lodepng_error_text(error);
		return false;
	}
	return true;
}

} // namespace uipreview

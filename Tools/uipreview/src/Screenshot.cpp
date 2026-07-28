#include "Screenshot.h"

#include "RmlUi_Include_GL3.h"

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
	glReadBuffer(GL_BACK);
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

bool WritePng(const std::string& path, const std::vector<unsigned char>& rgba, int width, int height, std::string& out_error)
{
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(path).parent_path();
	if (!parent.empty())
		std::filesystem::create_directories(parent, ec);

	if (const unsigned error = lodepng::encode(path, rgba, (unsigned)width, (unsigned)height))
	{
		out_error = lodepng_error_text(error);
		return false;
	}
	return true;
}

} // namespace uipreview

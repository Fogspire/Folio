#pragma once

#include <string>
#include <vector>

namespace uipreview {

/**
 * Reads the default framebuffer.
 *
 * Must be called after the renderer's EndFrame and before the buffer swap: EndFrame is what
 * resolves the layer framebuffers into the default one, and the swap discards it.
 */
bool CaptureBackBuffer(int width, int height, std::vector<unsigned char>& out_rgba, std::string& out_error);

/** Writes RGBA pixels, top row first, as a PNG. */
bool WritePng(const std::string& path, const std::vector<unsigned char>& rgba, int width, int height, std::string& out_error);

} // namespace uipreview

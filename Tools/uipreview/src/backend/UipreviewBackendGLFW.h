/**
 * Forked from RmlUi 6.2 `Backends/RmlUi_Backend_GLFW_GL3.cpp`, at commit
 * 2230d1a6e8e0848ed87a5761e2a5160b2a175ba4.
 *
 * Four things the shipped backend cannot express, all of them Phase 1 requirements:
 *  - the render and system interfaces are supplied by the caller, so subclasses can be
 *    installed. Upstream holds both by value in a file-static struct, with no seam.
 *  - the window can be created hidden and without monitor scaling, for screenshot capture.
 *  - EndFrame and SwapBuffers are separate. The GL3 renderer only blits its layer
 *    framebuffers to the default framebuffer during EndFrame, so a capture has to happen
 *    after that and before the swap.
 *  - the caller owns the context's dimensions and dp-ratio. Upstream overwrites both from
 *    the window on the first ProcessEvents, which would discard --size and --dp-ratio.
 *
 * Otherwise kept close to upstream so diffs stay readable across an RmlUi version bump.
 */
#pragma once

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>

#include <functional>

namespace Rml {
class Context;
class RenderInterface;
class SystemInterface;
} // namespace Rml

class RenderInterface_GL3;
class SystemInterface_GLFW;
struct GLFWwindow;

namespace Backend {

using KeyDownCallback = bool (*)(Rml::Context* context, Rml::Input::KeyIdentifier key, int key_modifier,
	float native_dp_ratio, bool priority);

/** Constructed after the GL context exists, because both compile shaders or touch GL state. */
using RenderInterfaceFactory = std::function<Rml::UniquePtr<RenderInterface_GL3>()>;
using SystemInterfaceFactory = std::function<Rml::UniquePtr<SystemInterface_GLFW>()>;

struct Options {
	const char* window_title = "uipreview";
	int width = 1920;
	int height = 1080;
	bool resizable = true;
	/** False creates the window without ever showing it, for headless-ish rendering. */
	bool visible = true;
	bool vsync = true;
	/**
	 * GLFW window size is in screen coordinates. With monitor scaling on, a 1920x1080 window
	 * has a 2880x1620 framebuffer under 150% display scaling, and screenshots come out the
	 * wrong size. Off means the requested size is in framebuffer pixels.
	 */
	bool scale_to_monitor = false;
};

bool Initialize(const Options& options, const RenderInterfaceFactory& render_interface_factory,
	const SystemInterfaceFactory& system_interface_factory);
void Shutdown();

Rml::SystemInterface* GetSystemInterface();
Rml::RenderInterface* GetRenderInterface();
GLFWwindow* GetWindow();

/** Framebuffer size in pixels, which is what the viewport and any capture must use. */
void GetFramebufferSize(int& out_width, int& out_height);

/**
 * The framebuffer a capture should read from, or zero for the window's back buffer.
 *
 * Non-zero when the render is going somewhere other than the window, which is how a size larger than
 * the display is possible at all. A capture that read the back buffer then would read the window's,
 * which is a different size and usually empty.
 */
unsigned int GetCaptureFramebuffer();

/** Wall clock seconds since initialisation. */
double GetTime();

bool ProcessEvents(Rml::Context* context, KeyDownCallback key_down_callback = nullptr, bool power_save = false);
void RequestExit();

/**
 * Clears to nothing rather than to opaque black, for a frame that is going to become a texture.
 *
 * Both the layer the renderer composites into and the target it composites onto have to be
 * transparent, or the ground shows up in the alpha of whatever is baked - and only where the
 * effect is soft, which reads as the colour being wrong rather than the clear.
 */
void SetTransparentClear(bool value);

void BeginFrame();
/** Resolves the renderer's layers into the default framebuffer. Capture after this. */
void EndFrame();
void SwapBuffers();

} // namespace Backend

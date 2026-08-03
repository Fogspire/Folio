#include "RmlUi_Include_GL3.h"

#include "UipreviewBackendGLFW.h"

#include "RmlUi_Platform_GLFW.h"
#include "RmlUi_Renderer_GL3.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Profiling.h>

#include <GLFW/glfw3.h>

static void SetupCallbacks(GLFWwindow* window);

static void LogErrorFromGLFW(int error, const char* description)
{
	Rml::Log::Message(Rml::Log::LT_ERROR, "GLFW error (0x%x): %s", error, description);
}

/**
 * Global data used by this backend.
 *
 * Lifetime governed by the calls to Backend::Initialize() and Backend::Shutdown().
 */
/**
 * A render target that is not the window's.
 *
 * A window's framebuffer is whatever the platform was willing to give it, and one larger than the
 * display is silently clamped to the work area - so a headless render above the display resolution
 * comes back smaller having reported success. Everything RmlUi draws happens at the viewport size in
 * its own layer framebuffers; only the final present is bound to the window. So this exists purely to
 * catch that present, through the SetTargetFramebuffer the GL3 patch adds.
 */
struct OffscreenTarget {
	GLuint framebuffer = 0;
	GLuint colour = 0;
	int width = 0;
	int height = 0;

	bool Create(int width, int height)
	{
		glGenTextures(1, &colour);
		glBindTexture(GL_TEXTURE_2D, colour);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);

		// Colour only: the present is a single textured quad, and every layer, stencil and clip mask
		// RmlUi needs lives in its own framebuffers rather than this one.
		const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE
			&& glGetError() == GL_NO_ERROR;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		if (!complete)
		{
			Destroy();
			return false;
		}

		this->width = width;
		this->height = height;
		return true;
	}

	void Destroy()
	{
		if (framebuffer) { glDeleteFramebuffers(1, &framebuffer); framebuffer = 0; }
		if (colour) { glDeleteTextures(1, &colour); colour = 0; }
		width = height = 0;
	}
};

struct BackendData {
	Rml::UniquePtr<SystemInterface_GLFW> system_interface;
	Rml::UniquePtr<RenderInterface_GL3> render_interface;
	GLFWwindow* window = nullptr;
	int glfw_active_modifiers = 0;
	OffscreenTarget offscreen;

	// Arguments set during event processing and nulled otherwise.
	Rml::Context* context = nullptr;
	Backend::KeyDownCallback key_down_callback = nullptr;

	bool transparent_clear = false;
};
static Rml::UniquePtr<BackendData> data;

bool Backend::Initialize(const Options& options, const RenderInterfaceFactory& render_interface_factory,
	const SystemInterfaceFactory& system_interface_factory)
{
	RMLUI_ASSERT(!data);

	glfwSetErrorCallback(LogErrorFromGLFW);

	if (!glfwInit())
		return false;

	// Set window hints for OpenGL 3.3 Core context creation.
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

	// Apply window properties and create it.
	glfwWindowHint(GLFW_RESIZABLE, options.resizable ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, options.scale_to_monitor ? GLFW_TRUE : GLFW_FALSE);
	glfwWindowHint(GLFW_VISIBLE, options.visible ? GLFW_TRUE : GLFW_FALSE);
	// So a capture can carry transparency rather than compositing against an opaque clear.
	glfwWindowHint(GLFW_ALPHA_BITS, 8);

	GLFWwindow* window = glfwCreateWindow(options.width, options.height, options.window_title, nullptr, nullptr);
	if (!window)
		return false;

	glfwMakeContextCurrent(window);
	glfwSwapInterval(options.vsync ? 1 : 0);

	// Load the OpenGL functions.
	Rml::String renderer_message;
	if (!RmlGL3::Initialize(&renderer_message))
		return false;

	data = Rml::MakeUnique<BackendData>();
	if (!data)
		return false;

	// Constructing the render interface compiles every shader, so it has to follow the GL
	// function load above. A null result means the shaders failed to build.
	data->render_interface = render_interface_factory ? render_interface_factory() : Rml::MakeUnique<RenderInterface_GL3>();
	if (!data->render_interface || !*data->render_interface)
		return false;

	data->system_interface = system_interface_factory ? system_interface_factory() : Rml::MakeUnique<SystemInterface_GLFW>();
	if (!data->system_interface)
		return false;

	data->window = window;
	data->system_interface->SetWindow(window);
	data->system_interface->LogMessage(Rml::Log::LT_INFO, renderer_message);

	// The window size is in screen coordinates and may differ from the framebuffer.
	int framebuffer_width = 0, framebuffer_height = 0;
	glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);

	// A hidden window is only a context to render through, so what the platform gave it does not
	// matter. Rendering at the size that was actually asked for does.
	if (!options.visible && (framebuffer_width != options.width || framebuffer_height != options.height))
	{
		if (data->offscreen.Create(options.width, options.height))
		{
			data->render_interface->SetTargetFramebuffer(data->offscreen.framebuffer);
			framebuffer_width = options.width;
			framebuffer_height = options.height;
		}
	}

	data->render_interface->SetViewport(framebuffer_width, framebuffer_height);

	// Receive num lock and caps lock modifiers for proper handling of numpad inputs in text fields.
	glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

	SetupCallbacks(window);

	return true;
}

void Backend::Shutdown()
{
	RMLUI_ASSERT(data);
	data->offscreen.Destroy();
	glfwDestroyWindow(data->window);
	data.reset();
	RmlGL3::Shutdown();
	glfwTerminate();
}

Rml::SystemInterface* Backend::GetSystemInterface()
{
	RMLUI_ASSERT(data);
	return data->system_interface.get();
}

Rml::RenderInterface* Backend::GetRenderInterface()
{
	RMLUI_ASSERT(data);
	return data->render_interface.get();
}

GLFWwindow* Backend::GetWindow()
{
	RMLUI_ASSERT(data);
	return data->window;
}

unsigned int Backend::GetCaptureFramebuffer()
{
	return data ? data->offscreen.framebuffer : 0;
}

void Backend::GetFramebufferSize(int& out_width, int& out_height)
{
	// The offscreen target when there is one: it, not the window, is what was rendered into and what
	// a capture reads. Reporting the window's size here is what made a large render come back with the
	// window's height and the requested width.
	if (data && data->offscreen.framebuffer)
	{
		out_width = data->offscreen.width;
		out_height = data->offscreen.height;
		return;
	}

	RMLUI_ASSERT(data);
	glfwGetFramebufferSize(data->window, &out_width, &out_height);
}

double Backend::GetTime()
{
	return glfwGetTime();
}

bool Backend::ProcessEvents(Rml::Context* context, KeyDownCallback key_down_callback, bool power_save)
{
	RMLUI_ASSERT(data && context);

	data->context = context;
	data->key_down_callback = key_down_callback;

	if (power_save)
		glfwWaitEventsTimeout(Rml::Math::Min(context->GetNextUpdateDelay(), 10.0));
	else
		glfwPollEvents();

	data->context = nullptr;
	data->key_down_callback = nullptr;

	const bool result = !glfwWindowShouldClose(data->window);
	glfwSetWindowShouldClose(data->window, GLFW_FALSE);
	return result;
}

void Backend::RequestExit()
{
	RMLUI_ASSERT(data);
	glfwSetWindowShouldClose(data->window, GLFW_TRUE);
}

void Backend::SetTransparentClear(const bool value)
{
	RMLUI_ASSERT(data);
	data->transparent_clear = value;
}

void Backend::BeginFrame()
{
	RMLUI_ASSERT(data);

	if (data->transparent_clear)
	{
		// Set before the renderer's own BeginFrame, which clears its first layer with whatever
		// clear colour is current. Both that layer and this target have to be empty.
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	else
	{
		data->render_interface->Clear();
	}

	data->render_interface->BeginFrame();
}

void Backend::EndFrame()
{
	RMLUI_ASSERT(data);
	data->render_interface->EndFrame();
}

void Backend::SwapBuffers()
{
	RMLUI_ASSERT(data);
	glfwSwapBuffers(data->window);

	// Optional, used to mark frames during performance profiling.
	RMLUI_FrameMark;
}

static void SetupCallbacks(GLFWwindow* window)
{
	RMLUI_ASSERT(data);

	// Key input
	glfwSetKeyCallback(window, [](GLFWwindow* /*window*/, int glfw_key, int /*scancode*/, int glfw_action, int glfw_mods) {
		if (!data->context)
			return;

		// Store the active modifiers for later because GLFW doesn't provide them in the callbacks to the mouse input events.
		data->glfw_active_modifiers = glfw_mods;

		Rml::Context* context = data->context;
		Backend::KeyDownCallback key_down_callback = data->key_down_callback;

		switch (glfw_action)
		{
		case GLFW_PRESS:
		case GLFW_REPEAT:
		{
			const Rml::Input::KeyIdentifier key = RmlGLFW::ConvertKey(glfw_key);
			const int key_modifier = RmlGLFW::ConvertKeyModifiers(glfw_mods);
			float dp_ratio = 1.f;
			glfwGetWindowContentScale(data->window, &dp_ratio, nullptr);

			// See if we have any global shortcuts that take priority over the context.
			if (key_down_callback && !key_down_callback(context, key, key_modifier, dp_ratio, true))
				break;
			// Otherwise, hand the event over to the context by calling the input handler as normal.
			if (!RmlGLFW::ProcessKeyCallback(context, glfw_key, glfw_action, glfw_mods))
				break;
			// The key was not consumed by the context either, try keyboard shortcuts of lower priority.
			if (key_down_callback && !key_down_callback(context, key, key_modifier, dp_ratio, false))
				break;
		}
		break;
		case GLFW_RELEASE: RmlGLFW::ProcessKeyCallback(context, glfw_key, glfw_action, glfw_mods); break;
		}
	});

	glfwSetCharCallback(window, [](GLFWwindow* /*window*/, unsigned int codepoint) { RmlGLFW::ProcessCharCallback(data->context, codepoint); });

	glfwSetCursorEnterCallback(window, [](GLFWwindow* /*window*/, int entered) { RmlGLFW::ProcessCursorEnterCallback(data->context, entered); });

	// Mouse input
	glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
		RmlGLFW::ProcessCursorPosCallback(data->context, window, xpos, ypos, data->glfw_active_modifiers);
	});

	glfwSetMouseButtonCallback(window, [](GLFWwindow* /*window*/, int button, int action, int mods) {
		data->glfw_active_modifiers = mods;
		RmlGLFW::ProcessMouseButtonCallback(data->context, button, action, mods);
	});

	glfwSetScrollCallback(window, [](GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
		RmlGLFW::ProcessScrollCallback(data->context, yoffset, data->glfw_active_modifiers);
	});

	// Window events. The context dimensions are deliberately not touched here: the caller owns
	// them, so that --size and --dp-ratio survive.
	glfwSetFramebufferSizeCallback(window, [](GLFWwindow* /*window*/, int width, int height) {
		if (data->render_interface)
			data->render_interface->SetViewport(width, height);
	});
}

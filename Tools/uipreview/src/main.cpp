#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <GLFW/glfw3.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>

#include <cstdio>

int main(int /*argc*/, char** /*argv*/)
{
	std::printf("uipreview\n");
	std::printf("  RmlUi   %s\n", Rml::GetVersion().c_str());
	std::printf("  GLFW    %s\n", glfwGetVersionString());
	std::printf("  lodepng %s\n", LODEPNG_VERSION_STRING);
	std::printf("  json    %d.%d.%d\n", NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR,
		NLOHMANN_JSON_VERSION_PATCH);
	return 0;
}

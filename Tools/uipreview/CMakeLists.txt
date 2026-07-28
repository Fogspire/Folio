add_executable(uipreview
	src/main.cpp
	src/App.cpp
	src/Diagnostics.cpp
	src/FileInterface.cpp
	src/Options.cpp
	src/SystemInterface.cpp

	# Forked from RmlUi's Backends/RmlUi_Backend_GLFW_GL3.cpp. See the header for why.
	src/backend/UipreviewBackendGLFW.cpp

	"${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_GLFW.cpp"
	"${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp"
)

target_include_directories(uipreview PRIVATE
	src
	"${rmlui_SOURCE_DIR}/Backends"
)

target_compile_definitions(uipreview PRIVATE
	RMLUI_PLATFORM_GLFW
	RMLUI_RENDERER_GL3
	NOMINMAX
	WIN32_LEAN_AND_MEAN
)

target_link_libraries(uipreview PRIVATE
	RmlUi::Core
	RmlUi::Debugger
	glfw
	lodepng
	nlohmann_json::nlohmann_json
	opengl32
)

set_target_properties(uipreview PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_SOURCE_DIR}/bin/win64"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_SOURCE_DIR}/bin/win64"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_SOURCE_DIR}/bin/win64"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin/win64"
)

if(MSVC)
	target_compile_options(uipreview PRIVATE /W4 /permissive- /utf-8)
endif()

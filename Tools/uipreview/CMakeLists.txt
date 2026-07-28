add_executable(uipreview
	src/main.cpp
	src/App.cpp
	src/DataModel.cpp
	src/Diagnostics.cpp
	src/ElementPath.cpp
	src/FileInterface.cpp
	src/FileWatcher.cpp
	src/InputScript.cpp
	src/JsonReport.cpp
	src/Localization.cpp
	src/MissingAssets.cpp
	src/Options.cpp
	src/Reload.cpp
	src/RenderInterface.cpp
	src/Screenshot.cpp
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
	_CRT_SECURE_NO_WARNINGS
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

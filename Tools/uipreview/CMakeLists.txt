# Everything but the command line, as a library: the same document host is driven by the tool's own
# main and, compiled to WebAssembly, by Folio Studio.
add_library(uipreview_core STATIC
	src/AccessibilityReport.cpp
	src/App.cpp
	src/Bake.cpp
	src/ColourVision.cpp
	src/CoreHost.cpp
	src/DataModel.cpp
	src/Diagnostics.cpp
	src/ElementPath.cpp
	src/FileInterface.cpp
	src/FileWatcher.cpp
	src/FontCache.cpp
	src/InputScript.cpp
	src/JsonReport.cpp
	src/MetricsReport.cpp
	src/ProvenanceReport.cpp
	src/Localization.cpp
	src/MissingAssets.cpp
	src/MsdfProgram.cpp
	src/Options.cpp
	src/Paths.cpp
	src/PerformanceReport.cpp
	src/Reload.cpp
	src/RenderInterface.cpp
	src/Screenshot.cpp
	src/SystemInterface.cpp
	src/TraceReport.cpp

	# Forked from RmlUi's Backends/RmlUi_Backend_GLFW_GL3.cpp. See the header for why.
	src/backend/UipreviewBackendGLFW.cpp

	"${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_GLFW.cpp"
	"${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp"
)

target_include_directories(uipreview_core PUBLIC
	src
	"${rmlui_SOURCE_DIR}/Backends"
)

target_compile_definitions(uipreview_core PUBLIC
	RMLUI_PLATFORM_GLFW
	RMLUI_RENDERER_GL3
	_CRT_SECURE_NO_WARNINGS
	NOMINMAX
	WIN32_LEAN_AND_MEAN
)

target_link_libraries(uipreview_core PUBLIC
	RmlUi::Core
	RmlUi::Debugger
	folio_core
	glfw
	lodepng
	nlohmann_json::nlohmann_json
)

if(EMSCRIPTEN)
	target_link_options(uipreview_core PUBLIC
		-sMIN_WEBGL_VERSION=2
		-sMAX_WEBGL_VERSION=2
		-sALLOW_MEMORY_GROWTH
		-sSTACK_SIZE=4MB
		-sMODULARIZE
		-sENVIRONMENT=web
		-sFORCE_FILESYSTEM
	)
	set(FOLIO_UIPREVIEW_BIN "${CMAKE_SOURCE_DIR}/bin/wasm")
elseif(WIN32)
	target_link_libraries(uipreview_core PUBLIC opengl32)
	set(FOLIO_UIPREVIEW_BIN "${CMAKE_SOURCE_DIR}/bin/win64")
endif()

if(MSVC)
	target_compile_options(uipreview_core PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(uipreview src/main.cpp)
target_link_libraries(uipreview PRIVATE uipreview_core)

if(EMSCRIPTEN)
	# A factory rather than a script that runs on load: the caller creates an instance, fills its
	# filesystem, and only then calls main. Used to diff this build's renders against the native one.
	target_link_options(uipreview PRIVATE
		-sEXPORT_NAME=createUipreview
		-sINVOKE_RUN=0
		-sEXIT_RUNTIME=0
		-sEXPORTED_RUNTIME_METHODS=FS,ENV,callMain
	)
endif()

set_target_properties(uipreview PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY                "${FOLIO_UIPREVIEW_BIN}"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${FOLIO_UIPREVIEW_BIN}"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${FOLIO_UIPREVIEW_BIN}"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${FOLIO_UIPREVIEW_BIN}"
)

if(MSVC)
	target_compile_options(uipreview PRIVATE /W4 /permissive- /utf-8)
endif()

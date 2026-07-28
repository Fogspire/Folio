cmake_minimum_required(VERSION 3.25)
project(Folio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

include(FetchContent)

set(FOLIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${FOLIO_ROOT}/cmake/Pins.cmake")

find_package(Git REQUIRED)
folio_patch_command(RmlUi RMLUI_PATCH_COMMAND)
folio_patch_command(freetype FREETYPE_PATCH_COMMAND)
folio_patch_command(glfw GLFW_PATCH_COMMAND)

FetchContent_Declare(freetype
	GIT_REPOSITORY "${FOLIO_FREETYPE_REPOSITORY}"
	GIT_TAG        "${FOLIO_FREETYPE_TAG}"
	GIT_SHALLOW    FALSE
	PATCH_COMMAND  ${FREETYPE_PATCH_COMMAND}
)
FetchContent_Declare(glfw
	GIT_REPOSITORY "${FOLIO_GLFW_REPOSITORY}"
	GIT_TAG        "${FOLIO_GLFW_TAG}"
	PATCH_COMMAND  ${GLFW_PATCH_COMMAND}
)
FetchContent_Declare(RmlUi
	GIT_REPOSITORY "${FOLIO_RMLUI_REPOSITORY}"
	GIT_TAG        "${FOLIO_RMLUI_TAG}"
	PATCH_COMMAND  ${RMLUI_PATCH_COMMAND}
)
FetchContent_Declare(nlohmann_json
	GIT_REPOSITORY "${FOLIO_JSON_REPOSITORY}"
	GIT_TAG        "${FOLIO_JSON_TAG}"
)
# lodepng ships no library target worth consuming, so SOURCE_SUBDIR points at a path with no
# CMakeLists.txt to suppress add_subdirectory. The target is declared by hand below.
FetchContent_Declare(lodepng
	GIT_REPOSITORY "${FOLIO_LODEPNG_REPOSITORY}"
	GIT_TAG        "${FOLIO_LODEPNG_TAG}"
	SOURCE_SUBDIR  "folio-no-cmake"
)

# Strip FreeType down to what RmlUi's font engine actually uses.
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)

set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

folio_set_rmlui_options()
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "RmlUi font engine (freetype or none)")

# RmlUi resolves its font engine through find_package(Freetype), but treats it as a soft
# dependency and only checks whether the Freetype::Freetype target exists. FreeType's own
# build declares that name as an EXPORT_NAME, which only applies to installed packages, so
# the alias has to be made here before RmlUi is configured.
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
	add_library(Freetype::Freetype ALIAS freetype)
endif()

FetchContent_MakeAvailable(glfw RmlUi nlohmann_json lodepng)

add_library(lodepng STATIC "${lodepng_SOURCE_DIR}/lodepng.cpp")
target_include_directories(lodepng PUBLIC "${lodepng_SOURCE_DIR}")

add_subdirectory(Tools/uipreview)

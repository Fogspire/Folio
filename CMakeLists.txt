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

# Dependencies are pinned to exact commits: git tags are movable, commit hashes are not.
# The tag each hash corresponds to is in the comment beside it.
FetchContent_Declare(freetype
	GIT_REPOSITORY "https://github.com/freetype/freetype.git"
	GIT_TAG        42608f77f20749dd6ddc9e0536788eaad70ea4b5 # VER-2-13-3
	GIT_SHALLOW    FALSE
)
FetchContent_Declare(glfw
	GIT_REPOSITORY "https://github.com/glfw/glfw.git"
	GIT_TAG        7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # 3.4
)
FetchContent_Declare(RmlUi
	GIT_REPOSITORY "https://github.com/mikke89/RmlUi.git"
	GIT_TAG        2230d1a6e8e0848ed87a5761e2a5160b2a175ba4 # 6.2
)
FetchContent_Declare(nlohmann_json
	GIT_REPOSITORY "https://github.com/nlohmann/json.git"
	GIT_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03 # v3.11.3
)
# lodepng ships no library target worth consuming, so SOURCE_SUBDIR points at a path with no
# CMakeLists.txt to suppress add_subdirectory. The target is declared by hand below.
FetchContent_Declare(lodepng
	GIT_REPOSITORY "https://github.com/lvandeve/lodepng.git"
	GIT_TAG        ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a
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

# RmlUi resolves its font engine through find_package(Freetype), but treats it as a soft
# dependency and only checks whether the Freetype::Freetype target exists. Adding FreeType
# first therefore satisfies it without any find-module involvement.
set(RMLUI_SAMPLES            OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS       OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN         OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN      OFF CACHE BOOL "" FORCE)
set(RMLUI_HARFBUZZ_SAMPLE    OFF CACHE BOOL "" FORCE)
set(RMLUI_INSTALL_TARGETS_DIR "" CACHE STRING "" FORCE)
# Left as a cache variable rather than forced: Phase 3 may need to set this to "none" and
# supply a UE-backed font engine, to avoid colliding with UE's own copy of FreeType.
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "RmlUi font engine (freetype or none)")

FetchContent_MakeAvailable(freetype glfw RmlUi nlohmann_json lodepng)

add_library(lodepng STATIC "${lodepng_SOURCE_DIR}/lodepng.cpp")
target_include_directories(lodepng PUBLIC "${lodepng_SOURCE_DIR}")

add_subdirectory(Tools/uipreview)

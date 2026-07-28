# Builds RmlUi as static libraries for the Unreal ThirdParty module.
#
# Driven by scripts/build_ue_libs.ps1, which supplies the toolset and the engine directory.
# Not part of the root build: the preview tool and the engine want different FreeType, a
# different runtime library and a different output location.
#
# The output lands in Source/ThirdParty/RmlUiLibrary/, which is what the .Build.cs consumes.

cmake_minimum_required(VERSION 3.25)
project(FolioUeLibs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED FOLIO_ENGINE_DIR)
	message(FATAL_ERROR "FOLIO_ENGINE_DIR must be set to the Unreal engine directory")
endif()

get_filename_component(FOLIO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${FOLIO_ROOT}/cmake/Pins.cmake")

find_package(Git REQUIRED)
folio_patch_command(RmlUi RMLUI_PATCH_COMMAND)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Unreal links the DLL runtime in every configuration a game ships in, and mixing runtimes
# across a static library boundary is an ODR violation that shows up as heap corruption
# rather than a link error.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Unreal disables RTTI and exceptions by default, and a static library that assumes otherwise
# will not link cleanly into a module that does not.
add_compile_options(/GR- /EHsc)

#
# FreeType: the engine's copy, not our own.
#
# Two FreeTypes in one link is a duplicate-symbol failure at best and silent, version-skewed
# aliasing at worst, since the engine already links its own for Slate. RmlUi resolves the font
# engine through find_package(Freetype) but only checks whether the Freetype::Freetype target
# exists, so an imported target pointing at the engine's build satisfies it.
#
set(FOLIO_FREETYPE_VERSION "FreeType2-2.14.1" CACHE STRING "Engine FreeType directory name")
set(FOLIO_FREETYPE_ROOT "${FOLIO_ENGINE_DIR}/Source/ThirdParty/FreeType2/${FOLIO_FREETYPE_VERSION}")
set(FOLIO_FREETYPE_INCLUDE "${FOLIO_FREETYPE_ROOT}/include")
set(FOLIO_FREETYPE_LIB "${FOLIO_FREETYPE_ROOT}/lib/Win64/x64/Release/freetype.lib")

if(NOT EXISTS "${FOLIO_FREETYPE_LIB}")
	message(FATAL_ERROR
		"Engine FreeType not found at ${FOLIO_FREETYPE_LIB}.\n"
		"Set FOLIO_FREETYPE_VERSION if this engine ships a different version.")
endif()

add_library(Freetype::Freetype STATIC IMPORTED GLOBAL)
set_target_properties(Freetype::Freetype PROPERTIES
	IMPORTED_LOCATION "${FOLIO_FREETYPE_LIB}"
	INTERFACE_INCLUDE_DIRECTORIES "${FOLIO_FREETYPE_INCLUDE}")

set(FREETYPE_FOUND TRUE)
set(FREETYPE_INCLUDE_DIRS "${FOLIO_FREETYPE_INCLUDE}")
set(FREETYPE_LIBRARIES "${FOLIO_FREETYPE_LIB}")

include(FetchContent)

folio_set_rmlui_options()
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)

FetchContent_Declare(RmlUi
	GIT_REPOSITORY "${FOLIO_RMLUI_REPOSITORY}"
	GIT_TAG        "${FOLIO_RMLUI_TAG}"
	PATCH_COMMAND  ${RMLUI_PATCH_COMMAND}
)
FetchContent_MakeAvailable(RmlUi)

#
# Stage the result where the .Build.cs expects it.
#
set(FOLIO_STAGE "${FOLIO_ROOT}/Source/ThirdParty/RmlUiLibrary")

add_custom_target(folio_stage ALL
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${FOLIO_STAGE}/include"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${rmlui_SOURCE_DIR}/Include" "${FOLIO_STAGE}/include"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${FOLIO_STAGE}/lib/Win64"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:rmlui_core>" "$<TARGET_FILE:rmlui_debugger>" "${FOLIO_STAGE}/lib/Win64/"
	DEPENDS rmlui_core rmlui_debugger
	COMMENT "Staging RmlUi into Source/ThirdParty/RmlUiLibrary"
	VERBATIM
)

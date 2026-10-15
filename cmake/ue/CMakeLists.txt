# Builds RmlUi as static libraries for the Unreal ThirdParty module.
#
# Driven by scripts/build_ue_libs.sh, which supplies the toolset and the engine directory.
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
folio_patch_command(msdfgen MSDFGEN_PATCH_COMMAND)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Unreal links the DLL runtime in every configuration a game ships in, and mixing runtimes
# across a static library boundary is an ODR violation that shows up as heap corruption
# rather than a link error.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Unreal disables RTTI by default, so RmlUi is built to use its own type system instead of
# language RTTI. Without this it calls dynamic_cast against classes compiled with /GR-, which
# is undefined behaviour rather than a compile error: it builds, links, and then dies inside
# Rml::CreateContext on the first cast.
#
# RMLUI_CUSTOM_RTTI is a PUBLIC definition, so RmlUiLibrary.Build.cs has to define it too.
# The header picks a different rmlui_dynamic_cast depending on it, and a library and a
# consumer that disagree are an ODR violation.
set(RMLUI_CUSTOM_RTTI ON CACHE BOOL "" FORCE)

# Which lib/ subdirectory the staged libraries land in, and the name the .Build.cs files look
# under. A cross build supplies its own along with a toolchain file.
set(FOLIO_UE_PLATFORM "Win64" CACHE STRING "Staging subdirectory under Source/ThirdParty/*/lib")

# Where the staged tree is written. A platform whose libraries are not distributed with the
# plugin points this outside it.
set(FOLIO_STAGE_ROOT "${FOLIO_ROOT}/Source/ThirdParty" CACHE PATH "Root the libraries stage into")

if(MSVC)
	add_compile_options(/GR- /EHsc)
else()
	add_compile_options(-fno-rtti -fexceptions)
	# An editor build on Mac and Linux links modules as shared libraries, which a static library
	# without position-independent code cannot go into.
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

#
# FreeType: the engine's copy, not our own.
#
# Two FreeTypes in one link is a duplicate-symbol failure at best and silent, version-skewed
# aliasing at worst, since the engine already links its own for Slate. RmlUi and msdfgen's
# extensions library both resolve it through find_package(Freetype) but only check whether the
# Freetype::Freetype target exists, so an imported target pointing at the engine's build
# satisfies them. FolioCore consumes the same library through the engine's FreeType2 module.
#
# It must be a build with TT_CONFIG_OPTION_GX_VAR_SUPPORT, or variable fonts silently degrade to
# their default instance. 2.14.1 has it; FolioFontFace probes FT_Get_MM_Var rather than trusting
# the version.
#
set(FOLIO_FREETYPE_VERSION "FreeType2-2.14.1" CACHE STRING "Engine FreeType directory name")
set(FOLIO_FREETYPE_ROOT "${FOLIO_ENGINE_DIR}/Source/ThirdParty/FreeType2/${FOLIO_FREETYPE_VERSION}")
set(FOLIO_FREETYPE_INCLUDE "${FOLIO_FREETYPE_ROOT}/include")

# The headers are the engine's for every platform, but the library is wherever that platform's
# build of the engine put it, so a cross build overrides only this.
if(FOLIO_UE_PLATFORM STREQUAL "Mac")
	set(FOLIO_FREETYPE_DEFAULT "${FOLIO_FREETYPE_ROOT}/lib/Mac/Release/libfreetype.a")
elseif(FOLIO_UE_PLATFORM STREQUAL "Linux")
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
		set(FOLIO_FREETYPE_DEFAULT "${FOLIO_FREETYPE_ROOT}/lib/Unix/aarch64-unknown-linux-gnueabi/Release/libfreetype.a")
	else()
		set(FOLIO_FREETYPE_DEFAULT "${FOLIO_FREETYPE_ROOT}/lib/Unix/x86_64-unknown-linux-gnu/Release/libfreetype.a")
	endif()
else()
	set(FOLIO_FREETYPE_DEFAULT "${FOLIO_FREETYPE_ROOT}/lib/Win64/x64/Release/freetype.lib")
endif()
set(FOLIO_FREETYPE_LIB "${FOLIO_FREETYPE_DEFAULT}"
	CACHE FILEPATH "Engine FreeType static library to link against")

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

# Text comes from FolioCore's distance-field engine, so RmlUi's own is not built. With "none",
# Rml::Initialise() fails outright unless SetFontEngineInterface was called first - a loud
# failure rather than a context that renders no text.
set(RMLUI_FONT_ENGINE "none" CACHE STRING "" FORCE)

folio_set_msdfgen_options()
folio_add_skia()

FetchContent_Declare(RmlUi
	GIT_REPOSITORY "${FOLIO_RMLUI_REPOSITORY}"
	GIT_TAG        "${FOLIO_RMLUI_TAG}"
	PATCH_COMMAND  ${RMLUI_PATCH_COMMAND}
)
FetchContent_Declare(msdfgen
	GIT_REPOSITORY "${FOLIO_MSDFGEN_REPOSITORY}"
	GIT_TAG        "${FOLIO_MSDFGEN_TAG}"
	PATCH_COMMAND  ${MSDFGEN_PATCH_COMMAND}
)
FetchContent_MakeAvailable(RmlUi msdfgen)

#
# Stage the result where the .Build.cs files expect it.
#
set(FOLIO_STAGE "${FOLIO_STAGE_ROOT}/RmlUiLibrary")

# One line per patch the libraries were built with, which RmlUiLibrary.Build.cs compares against the
# patches in the tree, so a pull that adds or changes one asks for a rebuild instead of failing to compile.
file(GLOB FOLIO_RMLUI_PATCHES CONFIGURE_DEPENDS "${FOLIO_ROOT}/patches/RmlUi/*.patch")
list(SORT FOLIO_RMLUI_PATCHES)
set(FOLIO_RMLUI_PATCH_STAMP "")
foreach(patch IN LISTS FOLIO_RMLUI_PATCHES)
	get_filename_component(name "${patch}" NAME)
	file(SHA256 "${patch}" hash)
	string(APPEND FOLIO_RMLUI_PATCH_STAMP "${name} ${hash}\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/RMLUI_PATCHES" "${FOLIO_RMLUI_PATCH_STAMP}")

add_custom_target(folio_stage ALL
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${FOLIO_STAGE}/include"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${rmlui_SOURCE_DIR}/Include" "${FOLIO_STAGE}/include"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${FOLIO_STAGE}/lib/${FOLIO_UE_PLATFORM}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:rmlui_core>" "$<TARGET_FILE:rmlui_debugger>" "${FOLIO_STAGE}/lib/${FOLIO_UE_PLATFORM}/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/RMLUI_PATCHES" "${FOLIO_STAGE}/lib/${FOLIO_UE_PLATFORM}/"
	DEPENDS rmlui_core rmlui_debugger
	COMMENT "Staging RmlUi into Source/ThirdParty/RmlUiLibrary"
	VERBATIM
)

# msdfgen's include interface is its source root, and msdfgen.h reaches core/ and ext/ relatively,
# so the staged tree has to keep that shape.
set(FOLIO_MSDF_STAGE "${FOLIO_STAGE_ROOT}/MsdfgenLibrary")
file(GLOB FOLIO_MSDF_CORE_HEADERS "${msdfgen_SOURCE_DIR}/core/*.h" "${msdfgen_SOURCE_DIR}/core/*.hpp")
file(GLOB FOLIO_MSDF_EXT_HEADERS  "${msdfgen_SOURCE_DIR}/ext/*.h")

add_custom_target(folio_stage_msdfgen ALL
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${FOLIO_MSDF_STAGE}/include"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${FOLIO_MSDF_STAGE}/include/core" "${FOLIO_MSDF_STAGE}/include/ext" "${FOLIO_MSDF_STAGE}/lib/${FOLIO_UE_PLATFORM}"
	COMMAND ${CMAKE_COMMAND} -E copy
		"${msdfgen_SOURCE_DIR}/msdfgen.h" "${msdfgen_SOURCE_DIR}/msdfgen-ext.h" "${FOLIO_MSDF_STAGE}/include/"
	COMMAND ${CMAKE_COMMAND} -E copy ${FOLIO_MSDF_CORE_HEADERS} "${FOLIO_MSDF_STAGE}/include/core/"
	COMMAND ${CMAKE_COMMAND} -E copy ${FOLIO_MSDF_EXT_HEADERS}  "${FOLIO_MSDF_STAGE}/include/ext/"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:msdfgen-core>" "$<TARGET_FILE:msdfgen-ext>" "$<TARGET_FILE:skia>" "${FOLIO_MSDF_STAGE}/lib/${FOLIO_UE_PLATFORM}/"
	DEPENDS msdfgen-core msdfgen-ext skia
	COMMENT "Staging msdfgen into Source/ThirdParty/MsdfgenLibrary"
	VERBATIM
)

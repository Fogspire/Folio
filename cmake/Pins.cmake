# Dependency pins and the local-patch mechanism, shared by every build in this repo.
#
# There are two: the standalone preview tool at the root, and the Unreal ThirdParty libraries
# under cmake/ue. They must build the same RmlUi, so the pin lives here rather than in either.
#
# Pins are exact commits: git tags are movable, commit hashes are not. The tag each hash
# corresponds to is in the comment beside it.

set(FOLIO_RMLUI_REPOSITORY "https://github.com/mikke89/RmlUi.git")
set(FOLIO_RMLUI_TAG        2230d1a6e8e0848ed87a5761e2a5160b2a175ba4) # 6.2

set(FOLIO_FREETYPE_REPOSITORY "https://github.com/freetype/freetype.git")
set(FOLIO_FREETYPE_TAG        42608f77f20749dd6ddc9e0536788eaad70ea4b5) # VER-2-13-3

set(FOLIO_GLFW_REPOSITORY "https://github.com/glfw/glfw.git")
set(FOLIO_GLFW_TAG        7b6aead9fb88b3623e3b3725ebb42670cbe4c579) # 3.4

set(FOLIO_JSON_REPOSITORY "https://github.com/nlohmann/json.git")
set(FOLIO_JSON_TAG        9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03) # v3.11.3

set(FOLIO_LODEPNG_REPOSITORY "https://github.com/lvandeve/lodepng.git")
set(FOLIO_LODEPNG_TAG        ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a)

set(FOLIO_MSDFGEN_REPOSITORY "https://github.com/Chlumsky/msdfgen.git")
set(FOLIO_MSDFGEN_TAG        85e8b3d47b3d1a42e4a5ebda0a24fb1cc2e669e0) # v1.12

# Only the vector baking tool parses SVG, so nanosvg is fetched by the root build alone.
set(FOLIO_NANOSVG_REPOSITORY "https://github.com/memononen/nanosvg.git")
set(FOLIO_NANOSVG_TAG        239e102ec2c691f2902e20ace2ed36ee4a35cfe6)

# Pinning a release for a long project means eventually needing a fix that is on upstream's
# master but not in the tag. Any .patch file dropped in patches/<dep>/ is applied to that
# dependency at fetch time, in sorted order, so a local fix is a reviewable file in this repo
# rather than a fork or a manual step. Re-run with a clean build tree after adding one.
function(folio_patch_command dependency out_variable)
	set(directory "${FOLIO_ROOT}/patches/${dependency}")

	file(GLOB patches CONFIGURE_DEPENDS "${directory}/*.patch")
	if(NOT patches)
		set(${out_variable} "" PARENT_SCOPE)
		return()
	endif()

	# Through a script rather than `git apply` directly: FetchContent re-runs its patch command
	# on every re-configure, and git apply treats an already-applied patch as an error.
	set(${out_variable}
		${CMAKE_COMMAND}
			-DFOLIO_PATCH_DIR=${directory}
			-DFOLIO_SOURCE_DIR=<SOURCE_DIR>
			-P ${FOLIO_ROOT}/cmake/ApplyPatches.cmake
		PARENT_SCOPE)

	message(STATUS "Folio: patching ${dependency} with ${patches}")
endfunction()

# msdfgen, configured the same way in both builds so the two renderers generate identical fields.
#
# CORE_ONLY stays off because readFreetypeOutline lives in the extensions library; with SVG and
# PNG disabled that library's only dependency is FreeType, which both builds already resolve
# through the Freetype::Freetype target. SVG parsing is nanosvg's job in the baking tool, and
# variable-font support is disabled because we drive FT_Set_Var_Design_Coordinates directly.
#
# DYNAMIC_RUNTIME is not optional: msdfgen sets MSVC_RUNTIME_LIBRARY per target, which overrides
# CMAKE_MSVC_RUNTIME_LIBRARY, so without it the static runtime is linked into a /MD build.
#
# Skia is on, which is what makes resolveShapeGeometry exist. Without it, a font that draws a
# glyph as overlapping contours - which every heavy weight does - produces a distance field that
# is inconsistent inside the overlap, and a solid stem reconstructs as two thin strokes. It gets
# worse with weight: Inter is clean at Regular, visibly broken at Bold, badly broken at Black.
# Unreal hits the same wall and vendors the same amalgam, though only under WITH_EDITOR.
macro(folio_set_msdfgen_options)
	set(MSDFGEN_CORE_ONLY              OFF CACHE BOOL "" FORCE)
	set(MSDFGEN_BUILD_STANDALONE       OFF CACHE BOOL "" FORCE)
	set(MSDFGEN_USE_VCPKG              OFF CACHE BOOL "" FORCE)
	set(MSDFGEN_USE_SKIA               ON  CACHE BOOL "" FORCE)
	set(MSDFGEN_USE_OPENMP             OFF CACHE BOOL "" FORCE)
	set(MSDFGEN_INSTALL                OFF CACHE BOOL "" FORCE)
	set(MSDFGEN_DYNAMIC_RUNTIME        ON  CACHE BOOL "" FORCE)
	set(MSDFGEN_DISABLE_SVG            ON  CACHE BOOL "" FORCE)
	set(MSDFGEN_DISABLE_PNG            ON  CACHE BOOL "" FORCE)
	set(MSDFGEN_DISABLE_VARIABLE_FONTS ON  CACHE BOOL "" FORCE)
endmacro()

# msdfgen finds Skia through find_package unless a target called `skia` already exists, so
# declaring one first is enough to hand it our vendored amalgam. It has to be declared before
# msdfgen is configured.
function(folio_add_skia)
	set(dir "${FOLIO_ROOT}/Source/ThirdParty/SkiaSimplify")
	add_library(skia STATIC "${dir}/skia-simplify.cpp")
	target_include_directories(skia PUBLIC "${dir}" "${dir}/include")
	target_compile_features(skia PUBLIC cxx_std_17)
	# Not namespaced, despite the amalgam offering SKIA_SIMPLIFY_NAMESPACE: parts of it qualify
	# their own traits with :: and stop compiling once wrapped. Slate leaves it global for the
	# same reason. Nothing collides anyway - Slate only builds its copy under WITH_EDITOR, and an
	# editor build is modular, so each module carries its own; a monolithic game build compiles
	# Slate's out entirely and only this one remains.
	if(MSVC)
		target_compile_options(skia PRIVATE /w)
	endif()
endfunction()

# Everything RmlUi builds that we never use.
macro(folio_set_rmlui_options)
	set(RMLUI_SAMPLES             OFF CACHE BOOL "" FORCE)
	set(RMLUI_LUA_BINDINGS        OFF CACHE BOOL "" FORCE)
	set(RMLUI_SVG_PLUGIN          OFF CACHE BOOL "" FORCE)
	set(RMLUI_LOTTIE_PLUGIN       OFF CACHE BOOL "" FORCE)
	set(RMLUI_HARFBUZZ_SAMPLE     OFF CACHE BOOL "" FORCE)
	set(RMLUI_INSTALL_TARGETS_DIR "" CACHE STRING "" FORCE)
endmacro()

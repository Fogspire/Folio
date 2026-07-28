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

# Pinning a release for a long project means eventually needing a fix that is on upstream's
# master but not in the tag. Any .patch file dropped in patches/<dep>/ is applied to that
# dependency at fetch time, in sorted order, so a local fix is a reviewable file in this repo
# rather than a fork or a manual step. Re-run with a clean build tree after adding one.
function(folio_patch_command dependency out_variable)
	file(GLOB patches CONFIGURE_DEPENDS "${FOLIO_ROOT}/patches/${dependency}/*.patch")
	if(NOT patches)
		set(${out_variable} "" PARENT_SCOPE)
		return()
	endif()
	list(SORT patches)
	# --forward keeps a re-configure over an already-patched tree from failing.
	set(${out_variable} ${GIT_EXECUTABLE} apply --ignore-whitespace --forward ${patches} PARENT_SCOPE)
	message(STATUS "Folio: patching ${dependency} with ${patches}")
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

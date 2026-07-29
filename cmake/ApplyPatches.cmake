# Applies every patch in a directory to a dependency's source tree, skipping any that is
# already applied.
#
# Run with cmake -P. It exists because `git apply` has no equivalent of GNU patch's --forward:
# re-applying a patch is an error, and FetchContent re-runs its patch command whenever the build
# tree is re-configured. Detecting the already-applied case with --reverse --check is the only
# portable way to make that idempotent.
#
# Expects FOLIO_PATCH_DIR and FOLIO_SOURCE_DIR.

file(GLOB patches "${FOLIO_PATCH_DIR}/*.patch")
if(NOT patches)
	return()
endif()

list(SORT patches)

find_package(Git QUIET REQUIRED)

foreach(patch IN LISTS patches)
	get_filename_component(name "${patch}" NAME)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --reverse --check "${patch}"
		WORKING_DIRECTORY "${FOLIO_SOURCE_DIR}"
		RESULT_VARIABLE already_applied
		OUTPUT_QUIET ERROR_QUIET)

	if(already_applied EQUAL 0)
		message(STATUS "Folio: ${name} already applied")
		continue()
	endif()

	execute_process(
		COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace "${patch}"
		WORKING_DIRECTORY "${FOLIO_SOURCE_DIR}"
		RESULT_VARIABLE failed
		ERROR_VARIABLE error)

	if(failed)
		message(FATAL_ERROR "Folio: could not apply ${name}\n${error}")
	endif()

	message(STATUS "Folio: applied ${name}")
endforeach()

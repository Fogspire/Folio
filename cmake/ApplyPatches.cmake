# Applies every patch in a directory to a dependency's source tree, skipping any that is
# already applied.
#
# Run with cmake -P. It exists because `git apply` has no equivalent of GNU patch's --forward:
# re-applying a patch is an error, and FetchContent re-runs its patch command whenever the build
# tree is re-configured.
#
# Each patch applied is recorded with its hash in .folio-patches in the source tree, and a recorded
# patch is skipped. --reverse --check is only the fallback for a tree patched before that record
# existed: a later patch that changes lines an earlier one left as context makes the earlier one
# stop reversing cleanly, and it would then be applied a second time and fail.
#
# Expects FOLIO_PATCH_DIR and FOLIO_SOURCE_DIR.

file(GLOB patches "${FOLIO_PATCH_DIR}/*.patch")
if(NOT patches)
	return()
endif()

list(SORT patches)

find_package(Git QUIET REQUIRED)

set(record "${FOLIO_SOURCE_DIR}/.folio-patches")
set(recorded "")
if(EXISTS "${record}")
	file(STRINGS "${record}" recorded)
endif()

foreach(patch IN LISTS patches)
	get_filename_component(name "${patch}" NAME)
	file(SHA256 "${patch}" hash)
	set(entry "${name} ${hash}")

	list(FIND recorded "${entry}" recorded_index)
	if(NOT recorded_index EQUAL -1)
		message(STATUS "Folio: ${name} already applied")
		continue()
	endif()

	execute_process(
		COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --reverse --check "${patch}"
		WORKING_DIRECTORY "${FOLIO_SOURCE_DIR}"
		RESULT_VARIABLE already_applied
		OUTPUT_QUIET ERROR_QUIET)

	if(already_applied EQUAL 0)
		message(STATUS "Folio: ${name} already applied")
	else()
		execute_process(
			COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace "${patch}"
			WORKING_DIRECTORY "${FOLIO_SOURCE_DIR}"
			RESULT_VARIABLE failed
			ERROR_VARIABLE error)

		if(failed)
			message(FATAL_ERROR "Folio: could not apply ${name}\n${error}")
		endif()

		message(STATUS "Folio: applied ${name}")
	endif()

	list(APPEND recorded "${entry}")
	list(JOIN recorded "\n" contents)
	file(WRITE "${record}" "${contents}\n")
endforeach()

# The document host Folio Studio draws its canvas with: uipreview's core behind a small embind
# surface, so the editor shows what the tool and the game show rather than an approximation.
#
# The output is platform-neutral and committed, so building Studio needs Node and nothing else.
add_executable(folio_engine src/Engine.cpp)
target_link_libraries(folio_engine PRIVATE uipreview_core)

target_link_options(folio_engine PRIVATE
	-lembind
	-sEXPORT_NAME=createFolioEngine
	-sEXPORTED_RUNTIME_METHODS=FS,ENV
)

set_target_properties(folio_engine PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_CURRENT_SOURCE_DIR}/dist"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)

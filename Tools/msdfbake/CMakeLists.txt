add_executable(msdfbake
	src/main.cpp
	src/BakeSink.cpp
	src/BakeVector.cpp
)

# The shape generator is private to FolioCore. The baking tool drives the identical code rather
# than a second copy, which is the whole reason a baked icon and a glyph resolve the same way.
target_include_directories(msdfbake PRIVATE
	"${CMAKE_SOURCE_DIR}/Source/FolioCore/Private"
)

target_link_libraries(msdfbake PRIVATE
	folio_core
	lodepng
	nanosvg
	# clip-path is an intersection, and this is the same library msdfgen unions contours with.
	skia
)

target_compile_definitions(msdfbake PRIVATE
	_CRT_SECURE_NO_WARNINGS
	NOMINMAX
	WIN32_LEAN_AND_MEAN
)

set_target_properties(msdfbake PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY                "${CMAKE_SOURCE_DIR}/bin/win64"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_SOURCE_DIR}/bin/win64"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_SOURCE_DIR}/bin/win64"
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/bin/win64"
)

if(MSVC)
	target_compile_options(msdfbake PRIVATE /W4 /permissive- /utf-8)
endif()

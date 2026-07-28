add_executable(msdfbake
	src/main.cpp
	src/BakeSink.cpp
)

target_link_libraries(msdfbake PRIVATE
	folio_core
	lodepng
	nanosvg
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

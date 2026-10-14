#pragma once

// WebGL2 is OpenGL ES 3.0 and is linked directly. Everywhere else the functions come from RmlUi's
// bundled loader, whose pointers are never filled in under Emscripten.
#ifdef __EMSCRIPTEN__
	#include <GLES3/gl3.h>
#else
	#include "RmlUi_Include_GL3.h"
#endif

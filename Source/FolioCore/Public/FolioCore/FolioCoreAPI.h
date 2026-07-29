#pragma once

// Unreal Build Tool defines FOLIOCORE_API as DLLEXPORT for this module's own translation units
// and DLLIMPORT for its consumers, so exported symbols cross the DLL boundary. Both spellings
// come from the platform header, which this module otherwise avoids: HAL/Platform.h is the
// smallest one that provides them and brings none of the macros - check, TEXT, min, max - that
// collide with msdfgen, FreeType and Skia.
//
// The same sources build into the preview tool through CMake, where the macro is absent and no
// export decoration is wanted.
#ifdef FOLIOCORE_API
	#include "HAL/Platform.h"
#else
	#define FOLIOCORE_API
#endif

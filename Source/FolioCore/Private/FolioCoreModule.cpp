#include "Modules/ModuleManager.h"

// The only file in this module that includes an Unreal header. The root CMakeLists.txt filters it
// out of the folio_core target, which builds every other Private/*.cpp for the preview tool.
IMPLEMENT_MODULE(FDefaultModuleImpl, FolioCore)

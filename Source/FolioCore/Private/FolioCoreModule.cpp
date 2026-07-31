#include "Modules/ModuleManager.h"

#if FOLIO_WITH_HARFBUZZ && !IS_MONOLITHIC
#include "HAL/UnrealMemory.h"

// HarfBuzz is compiled with its allocator redirected to these four names. Slate defines them and
// does not export them, so a second module linking the same static library has to supply its own.
// A monolithic build has one copy of the library and takes Slate's, which is why this is not
// compiled there.
extern "C"
{
	void* HarfBuzzMalloc(size_t SizeBytes)
	{
		return FMemory::Malloc(SizeBytes);
	}

	void* HarfBuzzCalloc(size_t Count, size_t ItemSizeBytes)
	{
		const size_t SizeBytes = Count * ItemSizeBytes;
		if (SizeBytes == 0)
		{
			return nullptr;
		}

		void* Memory = FMemory::Malloc(SizeBytes);
		FMemory::Memzero(Memory, SizeBytes);
		return Memory;
	}

	void* HarfBuzzRealloc(void* Memory, size_t SizeBytes)
	{
		return FMemory::Realloc(Memory, SizeBytes);
	}

	void HarfBuzzFree(void* Memory)
	{
		FMemory::Free(Memory);
	}
}
#endif

// The only file in this module that includes an Unreal header. The root CMakeLists.txt filters it
// out of the folio_core target, which builds every other Private/*.cpp for the preview tool.
IMPLEMENT_MODULE(FDefaultModuleImpl, FolioCore)

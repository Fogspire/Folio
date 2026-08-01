#include "FolioTextureLibrary.h"

#include "FolioTextures.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FolioTextureLibrary)

void UFolioTextureLibrary::RegisterFolioTexture(const FName Name, UTexture* Texture)
{
	Folio::RegisterTexture(Name, Texture);
}

void UFolioTextureLibrary::UnregisterFolioTexture(const FName Name)
{
	Folio::UnregisterTexture(Name);
}

UTexture* UFolioTextureLibrary::FindFolioTexture(const FName Name)
{
	return Folio::FindTexture(Name);
}

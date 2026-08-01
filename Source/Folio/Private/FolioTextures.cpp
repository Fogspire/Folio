#include "FolioTextures.h"

#include "FolioModule.h"
#include "FolioRenderInterface.h"
#include "FolioRmlUi.h"

#include "Engine/Texture.h"

#include <RmlUi/Core/Core.h>

namespace Folio
{
	const TCHAR* const TextureScheme = TEXT("folio://");

	namespace
	{
		TMap<FName, TStrongObjectPtr<UTexture>>& Registry()
		{
			static TMap<FName, TStrongObjectPtr<UTexture>> Textures;
			return Textures;
		}

		/**
		 * Drops RmlUi's cached handle for a name so the next draw resolves it again.
		 *
		 * The cache is keyed by source string, so without this a document that has already asked for
		 * a name keeps the texture it was given when it asked, and re-registering does nothing.
		 */
		void Invalidate(const FName Name)
		{
			if (!FFolioModule::IsAvailable())
			{
				return;
			}

			if (FFolioRenderInterface* Renderer = FFolioModule::Get().GetRenderInterface())
			{
				const FString Source = FString(TextureScheme) + Name.ToString();
				Rml::ReleaseTexture(ToRmlString(Source), Renderer);
			}
		}
	}

	void RegisterTexture(const FName Name, UTexture* Texture)
	{
		if (Name.IsNone())
		{
			return;
		}

		if (!Texture)
		{
			UnregisterTexture(Name);
			return;
		}

		if (const TStrongObjectPtr<UTexture>* Existing = Registry().Find(Name))
		{
			if (Existing->Get() && Existing->Get() != Texture)
			{
				UE_LOG(LogFolio, Warning,
					TEXT("texture '%s' was registered as '%s' and is now '%s'. Names are global, so ")
					TEXT("every document asking for it gets the new one. Prefix by screen to keep them apart."),
					*Name.ToString(), *Existing->Get()->GetName(), *Texture->GetName());
			}
		}

		Registry().Add(Name, TStrongObjectPtr<UTexture>(Texture));
		Invalidate(Name);
	}

	void UnregisterTexture(const FName Name)
	{
		if (Registry().Remove(Name) > 0)
		{
			Invalidate(Name);
		}
	}

	UTexture* FindTexture(const FName Name)
	{
		const TStrongObjectPtr<UTexture>* Found = Registry().Find(Name);
		return Found ? Found->Get() : nullptr;
	}

	FName ParseTextureName(const FString& Source)
	{
		if (!Source.StartsWith(TextureScheme, ESearchCase::IgnoreCase))
		{
			return NAME_None;
		}

		const FString Name = Source.RightChop(FCString::Strlen(TextureScheme));
		return Name.IsEmpty() ? NAME_None : FName(*Name);
	}

	FString PreviewFallbackPath(const FName Name)
	{
		return FString::Printf(TEXT("UI/art/preview/%s.png"), *Name.ToString());
	}
}

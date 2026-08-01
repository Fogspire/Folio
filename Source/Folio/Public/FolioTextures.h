#pragma once

#include "CoreMinimal.h"

class UTexture;

namespace Folio
{
	/** The scheme a document uses to name a texture the game provides: `folio://minimap`. */
	FOLIO_API extern const TCHAR* const TextureScheme;

	/**
	 * Textures the game hands to documents, by name.
	 *
	 * A document cannot reach an engine asset - `img src` and `decorator: image(...)` resolve through
	 * the file interface, which reads files. This is the seam for everything that is not a file: a
	 * scene capture behind a minimap, a paper doll, a rendered item, a video frame. Anything that is
	 * a `UTexture` at the point of use.
	 *
	 *     Folio::RegisterTexture(TEXT("hud/minimap"), CaptureTarget);
	 *     <img src="folio://hud/minimap"/>
	 *
	 * **Names are global.** RmlUi caches a texture against the source string on a render manager, and
	 * Folio has one for the whole application, so the same name in two documents is the same texture
	 * whichever registered it. Prefix names by screen when a collision is possible, exactly as sprite
	 * names are. Registering a name a different texture already holds is reported.
	 *
	 * Prefer `UFolioWidget::RegisterTexture`, which releases the names it registered when the widget
	 * goes away. These are for a caller that owns the lifetime itself.
	 */
	FOLIO_API void RegisterTexture(FName Name, UTexture* Texture);
	FOLIO_API void UnregisterTexture(FName Name);
	FOLIO_API UTexture* FindTexture(FName Name);

	/** The name in `folio://<name>`, or None when the source is not one. */
	FOLIO_API FName ParseTextureName(const FString& Source);

	/**
	 * Where the preview looks for a stand-in, relative to the project.
	 *
	 * A game texture cannot exist outside the engine, so `uipreview` draws this instead and the
	 * document lays out the same in both. Absent, it draws its usual placeholder.
	 */
	FOLIO_API FString PreviewFallbackPath(FName Name);
}

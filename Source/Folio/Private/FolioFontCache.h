#pragma once

#include "CoreMinimal.h"

#include "FolioCore/MsdfCache.h"

/**
 * Keeps generated glyph fields under the project's Saved directory.
 *
 * Generating one costs a few milliseconds, so a screen's worth of text is most of a second the
 * first time it is shown. That is a cost per machine rather than per run.
 *
 * Saved rather than anywhere in content: this is derived data keyed by font contents, it is
 * disposable, and it must be writable in a packaged build, which the content tree is not.
 */
class FFolioFontCache final : public Folio::IMsdfCacheStore
{
public:
	explicit FFolioFontCache(const FString& InDirectory);

	virtual bool Read(const std::string& Key, std::vector<unsigned char>& OutData) override;
	virtual void Write(const std::string& Key, const std::vector<unsigned char>& Data) override;

private:
	FString PathFor(const std::string& Key) const;

	FString Directory;
};

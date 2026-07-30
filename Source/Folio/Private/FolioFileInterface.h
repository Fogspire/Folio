#pragma once

#include "FolioRmlUi.h"

class IFileHandle;

/**
 * File access through IFileManager.
 *
 * Not an optimisation. RmlUi's default implementation uses fopen against the working
 * directory, which does not exist in a packaged build: content lives inside a pak file and
 * is only reachable through the engine's file system. Without this, a build that works in
 * the editor fails to load a single document once cooked.
 *
 * Paths arriving from RmlUi are either relative to the document that referenced them or
 * root-absolute. Root-absolute ones resolve against the content root rather than the disk
 * root, and the /folio/ prefix resolves against the plugin, so a document can reach the base
 * stylesheet from anywhere.
 */
class FFolioFileInterface final : public Rml::FileInterface
{
public:
	FFolioFileInterface(FString InContentRoot, FString InPluginRoot);

	virtual Rml::FileHandle Open(const Rml::String& Path) override;
	virtual void Close(Rml::FileHandle File) override;

	virtual size_t Read(void* Buffer, size_t Size, Rml::FileHandle File) override;
	virtual bool Seek(Rml::FileHandle File, long Offset, int Origin) override;
	virtual size_t Tell(Rml::FileHandle File) override;
	virtual size_t Length(Rml::FileHandle File) override;

	/** Absolute path a document reference resolves to, for diagnostics and hot reload. */
	FString Resolve(const Rml::String& Path) const;

	/**
	 * Everything RmlUi has opened, normalised for comparison against a watcher notification.
	 *
	 * This is the hot reload dependency list: the document, every @import-ed and <link>-ed
	 * stylesheet, every font and every texture, with no import parser to keep in step with
	 * RmlUi's own resolution rules. It only ever grows, so a file that is no longer referenced
	 * costs a wasted reload rather than a missed one.
	 */
	const TSet<FString>& GetOpened() const { return Opened; }

	/** Lowercased, forward slashed, absolute: the form GetOpened holds and compares against. */
	static FString NormalisePath(const FString& Path);

#if !UE_BUILD_SHIPPING
	/**
	 * A directory searched ahead of the package, so UI can be iterated on against a real build.
	 *
	 * This is what makes it possible to change a document on the target device - a console, a Steam
	 * Deck - without cooking again. Set from -uidev, and absent in Shipping.
	 */
	void SetOverrideRoot(FString Directory);
	const FString& GetOverrideRoot() const { return OverrideRoot; }

	/** Where -uidev points, or empty if it was not given. Called before the interface exists. */
	static FString OverrideRootFromCommandLine();
#endif

private:
	/** Resolution without the override, which is the ordinary content root and /folio/ mounts. */
	FString ResolveInPackage(const Rml::String& Path) const;

	FString ContentRoot;
	FString PluginRoot;
	TSet<FString> Opened;
#if !UE_BUILD_SHIPPING
	FString OverrideRoot;
#endif
};

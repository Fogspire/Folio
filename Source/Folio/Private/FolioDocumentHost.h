#pragma once

#include "FolioCore/FolioBehaviours.h"
#include "FolioInputState.h"

#include "CoreMinimal.h"

namespace Rml
{
	class Context;
	class ElementDocument;
}

/**
 * One RmlUi context and the document in it, with everything that is true of a document wherever it is
 * drawn: loading, reloading, data models, localisation, and deciding whether it needs updating at all.
 *
 * Extracted because a world-space document needs all of it and none of Slate. Duplicating it would
 * have meant two copies of the invalidation logic in particular, and the failure mode there is a
 * frozen document - the kind of bug that is quiet in one copy and loud in neither.
 *
 * What stays with the host that owns this: input, geometry, and how the thing reaches a screen.
 */
class FFolioDocumentHost
{
public:
	// Both out of line, for the members this header only forward declares. The destructor is the
	// obvious one; the constructor matters just as much, because it has to be able to unwind
	// already-built members and so needs them complete wherever it is instantiated. Leaving it
	// implicit compiles only while every including translation unit happens to be unity-built
	// alongside FolioDocumentHost.cpp, and breaks when adding a file elsewhere reshuffles the blobs.
	FFolioDocumentHost();
	~FFolioDocumentHost();

	/**
	 * Creates the context. Names are unique per process, so the label is only for diagnostics.
	 *
	 * A context needs a size before it lays anything out, so one is given up front rather than waiting
	 * for the first frame.
	 */
	bool Create(const TCHAR* Label, FIntPoint Size);
	void Destroy();

	bool IsValid() const { return Context != nullptr; }

	Rml::Context* GetContext() const { return Context; }
	Rml::ElementDocument* GetDocument() const { return Document; }

	/** Where the document came from, which is also what its gather namespace derives from. */
	const FString& GetDocumentPath() const { return DocumentPath; }

	/** Resizes the context. Marks dirty only when the size actually moved. */
	void SetSize(FIntPoint Size);
	FIntPoint GetSize() const { return Size; }

	/** Replaces the document. An empty path unloads without loading anything. */
	void LoadDocument(const FString& RelativePath);

	/** Rebuilds from disk, putting focus and scroll back. What a markup change needs. */
	void ReloadDocument();

	/** Re-reads stylesheets, leaving the DOM, focus, scroll and data models alone. */
	void ReloadStyleSheets();

	bool BindObjectModel(const FString& ModelName, UObject* Source, bool bPollEveryFrame);
	bool HasObjectModel(const FString& ModelName) const;

	/**
	 * Turns a theme on or off across every context, and remembers it for contexts made later.
	 *
	 * A theme in RmlUi is per-context and matched by `@media (theme: name)`, which is the only way a
	 * stylesheet can carry two looks at once: RCSS has no custom properties, so a theme cannot be a
	 * swapped value. A game sets one and every document answers, including documents not loaded yet.
	 */
	static void SetTheme(FName Theme, bool bActive);
	static bool IsThemeActive(FName Theme);
	static const TSet<FName>& GetActiveThemes();

	/** True while a text field has focus, so letters are text rather than directions. */
	bool IsTakingText() const;

	/** Says the document changed. The reason is what folio.Invalidation.Trace prints. */
	void MarkDirty(const TCHAR* Reason);

	/**
	 * Updates the document if anything needs it, and returns whether it did.
	 *
	 * `DpRatio` is what `dp` units resolve against. `RenderScale` is how many document units one
	 * rendered pixel is, which the background antialiasing works in.
	 *
	 * A false return means the frame was skipped because nothing had changed and RmlUi had asked not
	 * to be disturbed yet - so a caller that caches its output can keep the cache.
	 */
	bool Update(float DeltaSeconds, float DpRatio, float RenderScale);

	/** Polled sources are read here even when the update is skipped, so one still reports a change. */
	void TickDataModels();

	/**
	 * Called before the current document closes, while it is still alive.
	 *
	 * Anything holding a pointer into the document has to let go here rather than on the next load: by
	 * then the elements are freed, and removing a listener from one is a use-after-free rather than a
	 * no-op.
	 */
	TFunction<void()> OnDocumentClosing;

	/** Called after a document loads, for whatever the owner attaches to it. */
	TFunction<void(Rml::ElementDocument*)> OnDocumentLoaded;

	/** Called when the document is marked dirty, so an owner can invalidate its own cached output. */
	TFunction<void()> OnDirty;

	FFolioInputModel& GetInputModel() { return InputModel; }

private:
	/** Mounts ui/mock/<model>.json if the document declares a model, so bindings resolve. */
	void MountDataModel(const FString& RelativePath);

	Rml::Context* Context = nullptr;
	Rml::ElementDocument* Document = nullptr;
	FString DocumentPath;
	FIntPoint Size = FIntPoint(1920, 1080);

	FFolioInputModel InputModel;

	/** Dragging, dropping and closing, which every document gets whatever hosts it. */
	FFolioBehaviours Behaviours;

	/** Rebuilt with the context: a model cannot be added once a document is loaded. */
	TUniquePtr<class FFolioDataModel> DataModel;

	/** Bound UObject sources. Held here because a data model belongs to its context. */
	TArray<TUniquePtr<class FFolioObjectModel>> ObjectModels;

	/**
	 * Seconds until RmlUi says it must be updated again, from `Context::GetNextUpdateDelay()`.
	 *
	 * Infinity means nothing changes without input; zero means something is animating. Upstream
	 * requests it for a running transition, a blinking caret, a held scroll arrow and a smooth
	 * scroll, so those are not ours to track.
	 */
	double NextUpdateIn = 0.0;

	bool bDirty = true;
	const TCHAR* DirtyReason = TEXT("first frame");
};

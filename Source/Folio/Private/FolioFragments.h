#pragma once

#include "CoreMinimal.h"

#include <memory>

namespace Rml
{
	class Element;
	class ElementDocument;
	class StyleSheetContainer;
}

/**
 * The fragments one document host has been given, kept across reloads.
 *
 * A fragment outlives the document it is in: a reload or a late model binding rebuilds the document, and
 * every fragment goes back into its slot afterwards in the order it was added.
 */
class FFolioFragments
{
public:
	struct FFragment
	{
		int32 Handle = 0;
		FString SlotId;
		FString Path;

		/** The model this fragment bound for itself, released with it. Empty when it uses its slot's. */
		FString OwnModel;

		/** The element it was placed in, while the document holds it. */
		Rml::Element* Element = nullptr;

		std::shared_ptr<Rml::StyleSheetContainer> Styles;
	};

	/** Records a fragment and places it now if the document is up. */
	void Add(FFragment Fragment, Rml::ElementDocument* Document);

	/** Takes it out of the document and the list. False if no fragment has that handle. */
	bool Remove(int32 Handle, Rml::ElementDocument* Document, FString& OutOwnModel);

	/** Places every fragment into a document that has just loaded. */
	void Insert(Rml::ElementDocument* Document);

	/** Forgets the elements of a document that is closing, which frees them. */
	void Release();

	/** Re-reads every fragment's stylesheets after the document re-read its own, which dropped them. */
	void ReloadStyles(Rml::ElementDocument* Document);

	bool IsEmpty() const { return Fragments.IsEmpty(); }

private:
	void Place(FFragment& Fragment, Rml::ElementDocument* Document);
	void LoadStyles(FFragment& Fragment);
	void ApplyStyles(Rml::ElementDocument* Document);

	TArray<FFragment> Fragments;

	/** The document's own stylesheets, without any fragment's: what each change starts again from. */
	std::shared_ptr<Rml::StyleSheetContainer> DocumentStyles;
};

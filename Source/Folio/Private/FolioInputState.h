#pragma once

#include "FolioRmlUi.h"

#include "FolioCore/FolioInputState.h"

#include "CoreMinimal.h"

namespace Rml
{
	class Context;
	class ElementDocument;
}

/**
 * Publishes the input device and the confirm button into one context.
 *
 * Two routes, because markup wants them for different things. A body class covers presentation:
 * hiding the focus ring under a mouse, or picking a glyph decorator in RCSS with no data model
 * involved. The reserved 'folio' data model covers the rest, where an expression has to build a
 * value - a sprite name from the button, say. A game that only needs the first never binds the
 * model and pays nothing for it.
 *
 * One per context, since a data model belongs to a context. The values themselves are global and
 * live on the module.
 */
class FFolioInputModel
{
public:
	~FFolioInputModel();

	/** Creates the reserved model and starts listening for changes. */
	void Create(Rml::Context* Context);

	/** The document whose body carries the classes. Null when the context is torn down. */
	void SetDocument(Rml::ElementDocument* InDocument);

	/** Pushes the current values into the classes and the model. */
	void Refresh();

	void Release();

private:
	Rml::DataModelHandle Handle;
	Rml::ElementDocument* Document = nullptr;

	// Bound into the model by address, so they outlive it and are never reallocated elsewhere.
	Rml::String DeviceValue = "unknown";
	Rml::String ConfirmValue = "south";
	Rml::String BackValue = "east";
	bool bGamepad = false;

	FDelegateHandle DeviceChanged;
	FDelegateHandle ConfirmChanged;
};

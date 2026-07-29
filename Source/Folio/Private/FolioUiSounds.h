#pragma once

#include "FolioRmlUi.h"

namespace Rml
{
	class ElementDocument;
}

/**
 * The four moments a UI makes a noise, reported once so a game can play whatever it likes.
 *
 * Threading sound through a document after the fact means touching every component; a delegate
 * here costs nothing now and is the reason nobody has to. Folio plays nothing itself - it has no
 * business owning a sound asset.
 */
namespace Folio
{
	enum class EUiSound : uint8
	{
		/** Focus moved to a different element. */
		Focus,
		/** A focused element was activated. */
		Confirm,
		/** Back was pressed, whether from Escape or the gamepad. */
		Back,
		/** An activation that went nowhere, because the element is disabled. */
		Invalid,
	};

	const TCHAR* DescribeUiSound(EUiSound Sound);

	/**
	 * Listens to a document and reports the four moments.
	 *
	 * Attached to the document rather than to each element, so it keeps working as a screen's
	 * contents change and costs one listener per document.
	 */
	class FUiSoundListener final : public Rml::EventListener
	{
	public:
		void Attach(Rml::ElementDocument* Document);
		void Detach();

		void ProcessEvent(Rml::Event& Event) override;

	private:
		Rml::ElementDocument* Attached = nullptr;
	};
}

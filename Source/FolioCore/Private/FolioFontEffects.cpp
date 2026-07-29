#include "FolioCore/FolioFontEffects.h"

#include <set>

namespace Folio
{
	namespace
	{
		/// Every live Folio effect, so one can be recognised without a dynamic_cast.
		///
		/// Unreal builds RmlUi with RMLUI_CUSTOM_RTTI, so dynamic_cast is unavailable and the base
		/// class carries no type tag. Registering in the constructor and erasing in the destructor
		/// is cheap, and the set is only read while a font handle is being prepared.
		///
		/// This lives in the library rather than the header on purpose. The instancers are compiled
		/// per module, so a set defined inline would be per module too, and an effect built by one
		/// module would not be recognised by the font engine in the other.
		std::set<const Rml::FontEffect*>& LiveEffects()
		{
			static std::set<const Rml::FontEffect*> Effects;
			return Effects;
		}
	}

	FFolioFontEffect::FFolioFontEffect()
	{
		LiveEffects().insert(this);
	}

	FFolioFontEffect::~FFolioFontEffect()
	{
		LiveEffects().erase(this);
	}

	bool FFolioFontEffect::HasUniqueTexture() const
	{
		// Nothing is baked. The effect is the same field read at a different threshold, which is
		// what makes it cost an extra quad rather than an extra atlas.
		return false;
	}

	const FFolioFontEffect* AsFolioFontEffect(const Rml::FontEffect* Effect)
	{
		return LiveEffects().count(Effect) != 0 ? static_cast<const FFolioFontEffect*>(Effect) : nullptr;
	}
}

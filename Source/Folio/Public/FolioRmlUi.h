#pragma once

#include "CoreMinimal.h"

/**
 * The single place RmlUi is included from.
 *
 * RmlUi is ordinary C++ built outside Unreal's build system, so its headers have to be
 * bracketed to stop UBT's warning level and Unreal's macros from applying to them. Include
 * this header rather than <RmlUi/Core.h> anywhere in the plugin.
 */

THIRD_PARTY_INCLUDES_START

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/FileInterface.h>

THIRD_PARTY_INCLUDES_END

namespace Folio
{
	/**
	 * Rml::String is std::string holding UTF-8; FString is UTF-16. These are the only two
	 * conversions in the plugin, so every crossing of that boundary is one of these calls.
	 */
	inline FString ToFString(const Rml::String& Value)
	{
		return FString(UTF8_TO_TCHAR(Value.c_str()));
	}

	inline Rml::String ToRmlString(const FString& Value)
	{
		return Rml::String(TCHAR_TO_UTF8(*Value));
	}

	inline Rml::String ToRmlString(const TCHAR* Value)
	{
		return Rml::String(TCHAR_TO_UTF8(Value));
	}
}

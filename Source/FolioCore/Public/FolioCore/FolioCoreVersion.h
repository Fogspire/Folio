#pragma once

#include "FolioCore/FolioCoreAPI.h"

#include <string>

namespace Folio
{
	/// One line naming this module and the versions of RmlUi, FreeType and msdfgen it was built
	/// against. Reported by uipreview --font-report and the folio.Font.Report console command, so
	/// that "is the MSDF engine actually running" is answerable without a debugger.
	FOLIOCORE_API std::string GetCoreVersionString();
}

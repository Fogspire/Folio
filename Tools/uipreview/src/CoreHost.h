#pragma once

#include "FolioCore/FolioHost.h"

namespace uipreview {

class Diagnostics;
class RootedFileInterface;

/**
 * How FolioCore reaches this tool for files and logging.
 *
 * The tool links everything into one binary, so the core could reach RmlUi's globals directly.
 * It goes through the host anyway: the seam exists because Unreal cannot, and having both hosts
 * take the same path means a font that resolves here resolves there.
 */
class PreviewCoreHost final : public Folio::IFolioHost {
public:
	PreviewCoreHost(RootedFileInterface& files, Diagnostics& diagnostics);

	bool ReadFile(const std::string& path, std::vector<unsigned char>& out_data) override;
	void LogError(const std::string& message) override;
	void LogWarning(const std::string& message) override;

private:
	RootedFileInterface& files;
	Diagnostics& diagnostics;
};

} // namespace uipreview

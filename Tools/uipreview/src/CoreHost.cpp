#include "CoreHost.h"

#include "Diagnostics.h"
#include "FileInterface.h"

namespace uipreview {

PreviewCoreHost::PreviewCoreHost(RootedFileInterface& in_files, Diagnostics& in_diagnostics)
	: files(in_files), diagnostics(in_diagnostics)
{}

bool PreviewCoreHost::ReadFile(const std::string& path, std::vector<unsigned char>& out_data)
{
	const Rml::FileHandle handle = files.Open(path);
	if (!handle)
		return false;

	files.Seek(handle, 0, SEEK_END);
	const size_t length = files.Tell(handle);
	files.Seek(handle, 0, SEEK_SET);

	out_data.resize(length);
	const bool read = length > 0 && files.Read(out_data.data(), length, handle) == length;
	files.Close(handle);

	if (!read)
		out_data.clear();
	return read;
}

void PreviewCoreHost::LogError(const std::string& message)
{
	diagnostics.Report(Severity::Error, "E-FONT-CORE", message);
}

void PreviewCoreHost::LogWarning(const std::string& message)
{
	diagnostics.Report(Severity::Warning, "W-FONT-CORE", message);
}

} // namespace uipreview

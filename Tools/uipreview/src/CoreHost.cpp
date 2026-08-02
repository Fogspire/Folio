#include "CoreHost.h"

#include "Diagnostics.h"
#include "FileInterface.h"

namespace uipreview {

namespace {

/**
 * Core messages open with "folio <subsystem>: ", which is the only thing the host knows about
 * what a message is. Reporting all of them under one font code would make a broken term tag
 * look like a missing typeface.
 */
std::string CodeFor(const std::string& message, char severity)
{
	static const std::pair<const char*, const char*> kSubsystems[] = {
		{"folio text:", "TEXT-MARKUP"},
		{"folio terms:", "TEXT-TERMS"},
	};

	for (const auto& subsystem : kSubsystems)
	{
		if (message.rfind(subsystem.first, 0) == 0)
			return std::string(1, severity) + "-" + subsystem.second;
	}

	return std::string(1, severity) + "-FONT-CORE";
}

} // namespace

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
	diagnostics.Report(Severity::Error, CodeFor(message, 'E'), message);
}

void PreviewCoreHost::LogWarning(const std::string& message)
{
	diagnostics.Report(Severity::Warning, CodeFor(message, 'W'), message);
}

} // namespace uipreview

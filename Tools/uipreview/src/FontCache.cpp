#include "FontCache.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace uipreview {

FileFontCache::FileFontCache(std::string root) : directory(std::move(root))
{
	if (!directory.empty())
		directory += "/.folio-cache";
}

bool FileFontCache::Read(const std::string& key, std::vector<unsigned char>& out_data)
{
	if (directory.empty())
		return false;

	std::ifstream file(fs::path(directory) / (key + ".folioatlas"), std::ios::binary);
	if (!file)
		return false;

	out_data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	if (out_data.empty())
		return false;

	++hits;
	return true;
}

void FileFontCache::Write(const std::string& key, const std::vector<unsigned char>& data)
{
	if (directory.empty() || data.empty())
		return;

	std::error_code ec;
	fs::create_directories(directory, ec);

	// Written beside the target and moved into place, so a run that dies midway cannot leave a
	// truncated blob that the next run would read back as real fields.
	const fs::path final_path = fs::path(directory) / (key + ".folioatlas");
	const fs::path temp_path = fs::path(directory) / (key + ".folioatlas.tmp");

	{
		std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
		if (!file)
			return;
		file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
		if (!file)
			return;
	}

	fs::remove(final_path, ec);
	fs::rename(temp_path, final_path, ec);
	if (!ec)
		++writes;
}

} // namespace uipreview

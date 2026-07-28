#include "MissingAssets.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace uipreview {

namespace {

const char* kBeginMarker = "<!-- uipreview:begin -->";
const char* kEndMarker = "<!-- uipreview:end -->";

std::string Today()
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm {};
#ifdef _WIN32
	localtime_s(&tm, &now);
#else
	localtime_r(&now, &tm);
#endif
	char buffer[16];
	std::strftime(buffer, sizeof buffer, "%Y-%m-%d", &tm);
	return buffer;
}

// --- 5x7 glyphs, one byte per column, bit 0 is the top row ---------------------------------

struct Glyph {
	char character;
	unsigned char columns[5];
};

const Glyph kGlyphs[] = {
	{' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
	{'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
	{'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
	{'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
	{'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
	{'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
	{'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
	{'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
	{'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
	{'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
	{'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
	{'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
	{'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
	{'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
	{'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
	{'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
	{'A', {0x7C, 0x12, 0x11, 0x12, 0x7C}},
	{'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
	{'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
	{'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
	{'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
	{'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
	{'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
	{'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
	{'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
	{'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
	{'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
	{'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
	{'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
	{'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
	{'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
	{'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
	{'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
	{'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
	{'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
	{'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
	{'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
	{'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
	{'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
	{'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
	{'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
	{'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

const Glyph* FindGlyph(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	const Glyph* fallback = &kGlyphs[0];
	for (const Glyph& glyph : kGlyphs)
	{
		if (glyph.character == c)
			return &glyph;
		if (glyph.character == '?')
			fallback = &glyph;
	}
	return fallback;
}

void BlitPixel(std::vector<unsigned char>& pixels, int size, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	if (x < 0 || y < 0 || x >= size || y >= size)
		return;
	unsigned char* p = &pixels[((size_t)y * (size_t)size + (size_t)x) * 4];
	p[0] = r;
	p[1] = g;
	p[2] = b;
	p[3] = 255;
}

/** Draws text at a 2x scale, with a dark outline so it reads over either checker colour. */
void DrawText(std::vector<unsigned char>& pixels, int size, int origin_x, int origin_y, const std::string& text, int scale)
{
	int cursor_x = origin_x;
	for (const char c : text)
	{
		const Glyph* glyph = FindGlyph(c);
		for (int column = 0; column < 5; ++column)
		{
			for (int row = 0; row < 7; ++row)
			{
				if (!(glyph->columns[column] & (1 << row)))
					continue;
				for (int sy = 0; sy < scale; ++sy)
				{
					for (int sx = 0; sx < scale; ++sx)
					{
						const int px = cursor_x + column * scale + sx;
						const int py = origin_y + row * scale + sy;
						// Outline first, so neighbouring glyph pixels overwrite it back to white.
						BlitPixel(pixels, size, px + 1, py + 1, 0, 0, 0);
						BlitPixel(pixels, size, px, py, 255, 255, 255);
					}
				}
			}
		}
		cursor_x += 6 * scale;
	}
}

std::string Basename(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

void Merge(std::map<std::string, MissingAssets::Entry>& into, const std::string& name, const std::string& document)
{
	MissingAssets::Entry& entry = into[name];
	if (entry.name.empty())
	{
		entry.name = name;
		entry.first_seen = Today();
	}
	entry.last_seen = Today();
	if (!document.empty())
		entry.documents.insert(document);
}

/** Splits a markdown table row into its cells, ignoring the leading and trailing pipes. */
std::vector<std::string> SplitRow(const std::string& line)
{
	std::vector<std::string> cells;
	std::string cell;
	std::istringstream stream(line);
	while (std::getline(stream, cell, '|'))
	{
		std::string trimmed = cell;
		const size_t begin = trimmed.find_first_not_of(" \t`");
		const size_t end = trimmed.find_last_not_of(" \t`");
		cells.push_back(begin == std::string::npos ? std::string() : trimmed.substr(begin, end - begin + 1));
	}
	if (!cells.empty() && cells.front().empty())
		cells.erase(cells.begin());
	if (!cells.empty() && cells.back().empty())
		cells.pop_back();
	return cells;
}

std::set<std::string> SplitList(const std::string& text)
{
	std::set<std::string> out;
	std::istringstream stream(text);
	std::string item;
	while (std::getline(stream, item, ','))
	{
		const size_t begin = item.find_first_not_of(" \t`");
		const size_t end = item.find_last_not_of(" \t`");
		if (begin != std::string::npos)
			out.insert(item.substr(begin, end - begin + 1));
	}
	return out;
}

std::string JoinList(const std::set<std::string>& items)
{
	std::string out;
	for (const std::string& item : items)
	{
		if (!out.empty())
			out += ", ";
		out += "`" + item + "`";
	}
	return out;
}

} // namespace

std::vector<unsigned char> MakePlaceholderPixels(const std::string& name, int size)
{
	constexpr int kCell = 16;
	std::vector<unsigned char> pixels((size_t)size * (size_t)size * 4);

	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			const bool on = ((x / kCell) + (y / kCell)) % 2 == 0;
			unsigned char* p = &pixels[((size_t)y * (size_t)size + (size_t)x) * 4];
			// Fully opaque throughout, so premultiplying by alpha changes nothing.
			p[0] = on ? 255 : 0;
			p[1] = 0;
			p[2] = on ? 255 : 0;
			p[3] = 255;
		}
	}

	// Stamped twice at a fixed pitch, so a stretched or tiled decorator still shows a
	// readable name rather than a single smeared one.
	const std::string label = Basename(name);
	const int scale = 2;
	const int text_width = (int)label.size() * 6 * scale;
	DrawText(pixels, size, (size - text_width) / 2, size / 4 - 7, label, scale);
	DrawText(pixels, size, (size - text_width) / 2, (size * 3) / 4 - 7, label, scale);

	return pixels;
}

void MissingAssets::RecordSprite(const std::string& name)
{
	Merge(sprites, name, current_document);
}

void MissingAssets::RecordFileReference(const std::string& path)
{
	Merge(files, path, current_document);
}

void MissingAssets::RecordSize(const std::string& name, int width, int height)
{
	auto it = sprites.find(name);
	if (it == sprites.end())
		it = files.find(name);
	if (it == files.end() || it->second.name.empty())
		return;

	it->second.width = std::max(it->second.width, width);
	it->second.height = std::max(it->second.height, height);
}

bool MissingAssets::WriteMarkdown(const std::string& path, std::string& out_error) const
{
	// Read what is there, so entries and any hand-written notes outside the markers survive.
	std::string before;
	std::string after;
	std::map<std::string, Entry> merged_sprites = sprites;
	std::map<std::string, Entry> merged_files = files;

	{
		std::ifstream in(path);
		if (in)
		{
			std::string line;
			int section = 0; // 0 before markers, 1 inside, 2 after
			int table = 0;   // 1 sprites, 2 files
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();

				if (line.find(kBeginMarker) != std::string::npos)
				{
					section = 1;
					continue;
				}
				if (line.find(kEndMarker) != std::string::npos)
				{
					section = 2;
					continue;
				}

				if (section == 0)
				{
					before += line;
					before += '\n';
				}
				else if (section == 2)
				{
					after += line;
					after += '\n';
				}
				else
				{
					if (line.rfind("## Sprites", 0) == 0)
						table = 1;
					else if (line.rfind("## Unresolved", 0) == 0)
						table = 2;

					if (line.empty() || line[0] != '|')
						continue;

					const std::vector<std::string> cells = SplitRow(line);
					if (cells.size() < 3 || cells[0] == "sprite" || cells[0] == "path" || cells[0].find("---") == 0)
						continue;

					auto& target = (table == 1) ? merged_sprites : merged_files;
					Entry& entry = target[cells[0]];
					const bool is_new = entry.name.empty();
					if (is_new)
					{
						entry.name = cells[0];
						entry.first_seen = cells.size() > 3 ? cells[cells.size() - 2] : Today();
					}

					if (table == 1 && cells.size() >= 5)
					{
						int w = 0, h = 0;
						if (std::sscanf(cells[1].c_str(), "%dx%d", &w, &h) == 2)
						{
							entry.width = std::max(entry.width, w);
							entry.height = std::max(entry.height, h);
						}
						for (const std::string& doc : SplitList(cells[2]))
							entry.documents.insert(doc);
						if (entry.first_seen.empty() || cells[3] < entry.first_seen)
							entry.first_seen = cells[3];
						if (entry.last_seen.empty())
							entry.last_seen = cells[4];
					}
					else if (table == 2 && cells.size() >= 3)
					{
						for (const std::string& doc : SplitList(cells[1]))
							entry.documents.insert(doc);
						if (entry.last_seen.empty())
							entry.last_seen = cells[2];
					}
				}
			}
		}
	}

	if (before.empty())
	{
		before =
			"# Assets wanted\n"
			"\n"
			"Art that markup has asked for and that does not exist yet, collected by `uipreview`.\n"
			"Everything between the markers is rewritten on each run; notes below them are kept.\n"
			"\n";
	}

	std::ostringstream out;
	out << before;
	out << kBeginMarker << "\n\n";

	out << "## Sprites\n\n";
	if (merged_sprites.empty())
	{
		out << "None.\n\n";
	}
	else
	{
		out << "| sprite | max size | documents | first seen | last seen |\n";
		out << "| --- | --- | --- | --- | --- |\n";
		for (const auto& [name, entry] : merged_sprites)
		{
			out << "| `" << name << "` | " << entry.width << "x" << entry.height << " | " << JoinList(entry.documents)
				<< " | " << entry.first_seen << " | " << entry.last_seen << " |\n";
		}
		out << "\n";
	}

	out << "## Unresolved file references\n\n";
	if (merged_files.empty())
	{
		out << "None.\n\n";
	}
	else
	{
		out << "| path | documents | last seen |\n";
		out << "| --- | --- | --- |\n";
		for (const auto& [name, entry] : merged_files)
			out << "| `" << name << "` | " << JoinList(entry.documents) << " | " << entry.last_seen << " |\n";
		out << "\n";
	}

	out << kEndMarker << "\n";
	if (!after.empty())
		out << after;

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		out_error = "could not open for writing";
		return false;
	}
	file << out.str();
	return true;
}

} // namespace uipreview

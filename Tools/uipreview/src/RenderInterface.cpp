#include "RenderInterface.h"

#include "Diagnostics.h"
#include "MissingAssets.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <lodepng.h>

#include <algorithm>
#include <cmath>

namespace uipreview {

namespace {

constexpr int kPlaceholderSize = 128;

bool HasExtension(const std::string& source, const std::string& extension)
{
	if (source.size() < extension.size())
		return false;

	return std::equal(extension.rbegin(), extension.rend(), source.rbegin(),
		[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
}

/**
 * RmlUi's own texture loader reads uncompressed TGA and nothing else, so every real atlas
 * would fail to load. Reads through the file interface rather than the filesystem, which is
 * what makes root-relative paths and the /folio/ mount work.
 */
bool LoadPng(const std::string& source, std::vector<unsigned char>& out_pixels, Rml::Vector2i& out_size)
{
	Rml::FileInterface* files = Rml::GetFileInterface();
	Rml::FileHandle file = files->Open(source);
	if (!file)
		return false;

	files->Seek(file, 0, SEEK_END);
	const size_t length = files->Tell(file);
	files->Seek(file, 0, SEEK_SET);

	std::vector<unsigned char> encoded(length);
	const size_t read = files->Read(encoded.data(), length, file);
	files->Close(file);
	if (read != length)
		return false;

	unsigned width = 0;
	unsigned height = 0;
	if (lodepng::decode(out_pixels, width, height, encoded) != 0)
		return false;

	// RmlUi 6.x expects premultiplied alpha; lodepng decodes straight alpha.
	for (size_t i = 0; i + 3 < out_pixels.size(); i += 4)
	{
		const unsigned alpha = out_pixels[i + 3];
		out_pixels[i + 0] = (unsigned char)((out_pixels[i + 0] * alpha + 127) / 255);
		out_pixels[i + 1] = (unsigned char)((out_pixels[i + 1] * alpha + 127) / 255);
		out_pixels[i + 2] = (unsigned char)((out_pixels[i + 2] * alpha + 127) / 255);
	}

	out_size = {int(width), int(height)};
	return true;
}

std::string Basename(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

/**
 * An unresolved sprite name arrives here already joined against the document directory, so
 * `icon_sword` becomes `/ui/screens/icon_sword`. Path separators therefore say nothing about
 * what the markup wrote. A file extension does: sprite names do not carry one.
 */
bool LooksLikeFileReference(const std::string& source)
{
	const std::string name = Basename(source);
	const size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot + 1 >= name.size())
		return false;

	// Guard against a sprite name that merely contains a dot.
	const std::string extension = name.substr(dot + 1);
	return extension.size() <= 4 && std::all_of(extension.begin(), extension.end(), [](unsigned char c) { return std::isalnum(c); });
}

} // namespace

PreviewRenderInterface::PreviewRenderInterface(Diagnostics& in_diagnostics, MissingAssets& in_missing_assets)
	: diagnostics(in_diagnostics), missing_assets(in_missing_assets)
{}

Rml::TextureHandle PreviewRenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	if (HasExtension(source, ".png"))
	{
		std::vector<unsigned char> pixels;
		Rml::Vector2i size;
		if (LoadPng(source, pixels, size))
		{
			texture_dimensions = size;
			return RenderInterface_GL3::GenerateTexture(
				Rml::Span<const Rml::byte>(pixels.data(), pixels.size()), size);
		}
	}
	else if (const Rml::TextureHandle handle = RenderInterface_GL3::LoadTexture(texture_dimensions, source))
	{
		return handle;
	}

	const bool is_file = LooksLikeFileReference(source);
	const std::string name = is_file ? diagnostics.Relative(source) : Basename(source);

	if (const auto it = placeholder_textures.find(name); it != placeholder_textures.end())
	{
		texture_dimensions = {kPlaceholderSize, kPlaceholderSize};
		return it->second;
	}

	if (is_file)
	{
		missing_assets.RecordFileReference(name);
		diagnostics.Report(Severity::Warning, "W-ASSET-MISSINGFILE",
			"could not open '" + name + "'; rendered as a placeholder", {}, 0, {{"path", name}});
	}
	else
	{
		missing_assets.RecordSprite(name);
		diagnostics.Report(Severity::Warning, "W-ASSET-MISSING",
			"sprite '" + name + "' is not declared in any loaded @spritesheet; rendered as a placeholder", {}, 0,
			{{"sprite", name}});
	}

	const std::vector<unsigned char> pixels = MakePlaceholderPixels(name, kPlaceholderSize);
	texture_dimensions = {kPlaceholderSize, kPlaceholderSize};

	const Rml::TextureHandle handle = RenderInterface_GL3::GenerateTexture(
		Rml::Span<const Rml::byte>(pixels.data(), pixels.size()), texture_dimensions);

	placeholder_textures[name] = handle;
	placeholder_names[handle] = name;
	return handle;
}

Rml::CompiledGeometryHandle PreviewRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
	Rml::Span<const int> indices)
{
	const Rml::CompiledGeometryHandle handle = RenderInterface_GL3::CompileGeometry(vertices, indices);

	// LoadTexture runs before layout, so it cannot know the size a sprite is drawn at. The
	// geometry's bounds can, including whatever stretching the decorator applied.
	if (handle && !vertices.empty())
	{
		Rml::Vector2f minimum = vertices[0].position;
		Rml::Vector2f maximum = vertices[0].position;
		for (const Rml::Vertex& vertex : vertices)
		{
			minimum.x = std::min(minimum.x, vertex.position.x);
			minimum.y = std::min(minimum.y, vertex.position.y);
			maximum.x = std::max(maximum.x, vertex.position.x);
			maximum.y = std::max(maximum.y, vertex.position.y);
		}
		geometry_size[handle] = maximum - minimum;
	}

	return handle;
}

void PreviewRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	Rml::TextureHandle texture)
{
	if (const auto name = placeholder_names.find(texture); name != placeholder_names.end())
	{
		if (const auto size = geometry_size.find(geometry); size != geometry_size.end())
		{
			missing_assets.RecordSize(name->second, (int)std::lround(size->second.x), (int)std::lround(size->second.y));
		}
	}

	RenderInterface_GL3::RenderGeometry(geometry, translation, texture);
}

void PreviewRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	geometry_size.erase(geometry);
	RenderInterface_GL3::ReleaseGeometry(geometry);
}

void PreviewRenderInterface::ClearPlaceholders()
{
	for (const auto& [name, handle] : placeholder_textures)
		RenderInterface_GL3::ReleaseTexture(handle);

	placeholder_textures.clear();
	placeholder_names.clear();
}

} // namespace uipreview

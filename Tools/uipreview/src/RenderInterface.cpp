#include "RenderInterface.h"

#include <cstdio>

#include "FolioCore/MsdfSidecar.h"

#include "Diagnostics.h"
#include "MissingAssets.h"

#include "GL.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>

#include <lodepng.h>

#include <algorithm>
#include <cmath>
#include <string_view>

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
bool LoadPng(const std::string& source, std::vector<unsigned char>& out_pixels, Rml::Vector2i& out_size,
	bool premultiply = true)
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
	//
	// Never for a distance field. Its alpha is a distance, not an opacity, so scaling the three
	// channels by it is not a colour conversion - it erases the field everywhere the shape is
	// thin, and what survives is only the deep interior.
	for (size_t i = 0; premultiply && i + 3 < out_pixels.size(); i += 4)
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
 * `icon_sword` becomes `/UI/screens/icon_sword`. Path separators therefore say nothing about
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

/** The name in `folio://<name>`, or empty when the source is not one. */
std::string NamedTextureOf(const std::string& source)
{
	constexpr std::string_view scheme = "folio://";
	if (source.size() <= scheme.size() || source.compare(0, scheme.size(), scheme) != 0)
		return {};

	return source.substr(scheme.size());
}

} // namespace

PreviewRenderInterface::PreviewRenderInterface(Diagnostics& in_diagnostics, MissingAssets& in_missing_assets)
	: diagnostics(in_diagnostics), missing_assets(in_missing_assets)
{}

Rml::TextureHandle PreviewRenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	// A texture the game hands the document at runtime - a scene capture, a paper doll, a rendered
	// item. There is no such thing outside the engine, so a stand-in beside the document is what
	// keeps the layout honest here.
	if (const std::string named = NamedTextureOf(source); !named.empty())
	{
		const std::string fallback = "UI/art/preview/" + named + ".png";

		std::vector<unsigned char> pixels;
		Rml::Vector2i size;
		if (LoadPng(fallback, pixels, size))
		{
			texture_dimensions = size;
			return RenderInterface_GL3::GenerateTexture(
				Rml::Span<const Rml::byte>(pixels.data(), pixels.size()), size);
		}

		if (const auto it = placeholder_textures.find(named); it != placeholder_textures.end())
		{
			texture_dimensions = {kPlaceholderSize, kPlaceholderSize};
			return it->second;
		}

		// Info, not a warning: a document naming a game texture is correct, and it must still pass
		// --strict. The stand-in is an option, not an obligation.
		diagnostics.Report(Severity::Info, "I-ASSET-GAMETEXTURE",
			"'" + std::string(source) + "' is a texture the game provides; rendered as a placeholder. "
			"Put " + fallback + " beside the document for a stand-in.",
			{}, 0, {{"texture", named}, {"fallback", fallback}});

		const std::vector<unsigned char> pixels_placeholder = MakePlaceholderPixels(named, kPlaceholderSize);
		texture_dimensions = {kPlaceholderSize, kPlaceholderSize};

		const Rml::TextureHandle handle = RenderInterface_GL3::GenerateTexture(
			Rml::Span<const Rml::byte>(pixels_placeholder.data(), pixels_placeholder.size()), texture_dimensions);

		placeholder_textures[named] = handle;
		placeholder_names[handle] = named;
		return handle;
	}

	if (HasExtension(source, ".png"))
	{
		// A sheet baked by msdfbake carries a sidecar naming its field parameters. With it, the
		// image is distance rather than colour and resolves through the field shader, so a sprite
		// from it scales like text. Without it, this is ordinary art.
		const bool is_field = !Folio::MsdfSidecarPathFor(source).empty();

		std::vector<unsigned char> pixels;
		Rml::Vector2i size;
		if (LoadPng(source, pixels, size, !is_field))
		{
			texture_dimensions = size;

			if (is_field)
			{
				if (const Rml::TextureHandle field = LoadFieldAtlas(source, pixels, size))
					return field;
			}

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

void PreviewRenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	// The base class would pass the handle straight to glDeleteTextures. A field page's handle is
	// an index into msdf_bindings, not a GL name, so that deletes whatever texture happens to
	// share the number - which reads as random geometry losing its texture much later.
	if (msdf_bindings.find(texture) != msdf_bindings.end())
	{
		// Only a page RmlUi loaded may be torn down here. A glyph atlas reaches this function too,
		// because it is handed over as a callback texture and Rml::ReleaseTextures releases those
		// as well - but the font engine still owns it and keeps writing new glyphs into it. Freeing
		// it on a hot reload would delete the live atlas and leave every later glyph drawing from a
		// deleted texture. RmlUi re-runs the callback and gets the same handle back, which is
		// exactly what should happen.
		if (field_atlases_from_file.erase(texture) > 0)
			ReleaseAtlasHandle(Folio::FMsdfHandle(texture));
		return;
	}

	RenderInterface_GL3::ReleaseTexture(texture);
}

Rml::CompiledGeometryHandle PreviewRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
	Rml::Span<const int> indices)
{
	const Rml::CompiledGeometryHandle handle = RenderInterface_GL3::CompileGeometry(vertices, indices);

	// LoadTexture runs before layout, so it cannot know the size a sprite is drawn at. The
	// geometry's bounds can, including whatever stretching the decorator applied.
	if (handle && !vertices.empty())
	{
		ShadowGeometry& shadow = shadow_geometry[handle];

		Rml::Vector2f minimum = vertices[0].position;
		Rml::Vector2f maximum = vertices[0].position;
		for (const Rml::Vertex& vertex : vertices)
		{
			minimum.x = std::min(minimum.x, vertex.position.x);
			minimum.y = std::min(minimum.y, vertex.position.y);
			maximum.x = std::max(maximum.x, vertex.position.x);
			maximum.y = std::max(maximum.y, vertex.position.y);
		}
		shadow.size = maximum - minimum;

		// Whether this is a field is not known until it is drawn, and by then the vertices are
		// gone, so they are kept here. GL buffers are only built if a draw actually needs them.
		shadow.vertices.assign(vertices.begin(), vertices.end());
		shadow.indices.assign(indices.begin(), indices.end());
	}

	return handle;
}

void PreviewRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	Rml::TextureHandle texture)
{
	if (const auto name = placeholder_names.find(texture); name != placeholder_names.end())
	{
		if (const auto shadow = shadow_geometry.find(geometry); shadow != shadow_geometry.end())
		{
			missing_assets.RecordSize(name->second, (int)std::lround(shadow->second.size.x),
				(int)std::lround(shadow->second.size.y));
		}
	}

	const auto binding = msdf_bindings.find(texture);
	const auto shadow = shadow_geometry.find(geometry);

	if (binding == msdf_bindings.end() || shadow == shadow_geometry.end())
	{
		RenderInterface_GL3::RenderGeometry(geometry, translation, texture);
		return;
	}

	if (!msdf_program_ready)
	{
		msdf_program_ready = true;
		std::string error;
		if (!msdf_program.Initialise(error))
			diagnostics.Report(Severity::Error, "E-MSDF-SHADER",
				"the distance field shader would not build: " + error);
	}

	if (!msdf_program.IsValid())
	{
		RenderInterface_GL3::RenderGeometry(geometry, translation, texture);
		return;
	}

	if (shadow->second.msdf.vao == 0)
		shadow->second.msdf.Upload(shadow->second.vertices, shadow->second.indices);

	const unsigned int fill = binding->second.effect.FillImage.empty()
		? 0u
		: ResolveFill(binding->second.effect.FillImage);

	msdf_program.Bind(GetTransform(), translation, binding->second.texture, binding->second.unit_range,
		binding->second.effect, msdf_sharpen, fill, float(viewport.y));

	glBindVertexArray(shadow->second.msdf.vao);
	glDrawElements(GL_TRIANGLES, shadow->second.msdf.index_count, GL_UNSIGNED_INT, nullptr);
	glBindVertexArray(0);
	++msdf_draw_count;

	// The base class caches which program is bound, so it has to be told the binding is stale or
	// its next draw uploads its uniforms into ours.
	ResetProgram();
}

void PreviewRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	if (const auto shadow = shadow_geometry.find(geometry); shadow != shadow_geometry.end())
	{
		shadow->second.msdf.Release();
		shadow_geometry.erase(shadow);
	}

	RenderInterface_GL3::ReleaseGeometry(geometry);
}

Rml::TextureHandle PreviewRenderInterface::LoadFieldAtlas(const Rml::String& source,
	const std::vector<unsigned char>& pixels, Rml::Vector2i size)
{
	const std::string sidecar_path = Folio::MsdfSidecarPathFor(source);
	if (sidecar_path.empty())
		return {};

	Rml::String text;
	if (!Rml::GetFileInterface()->LoadFile(sidecar_path, text))
	{
		// The suffix is a promise the bake makes, so a missing sidecar is a broken pair rather
		// than ordinary art that happens to be named this way.
		diagnostics.Report(Severity::Warning, "W-MSDF-NOSIDECAR",
			"'" + diagnostics.Relative(source) + "' is named as a distance-field atlas but has no "
			"sidecar beside it; drawn as ordinary art", {}, 0, {{"path", diagnostics.Relative(source)}});
		return {};
	}

	Folio::FMsdfSidecar sidecar;
	if (!Folio::ParseMsdfSidecar(text, sidecar) || !sidecar.IsValid())
	{
		diagnostics.Report(Severity::Warning, "W-MSDF-BADSIDECAR",
			"the sidecar beside '" + diagnostics.Relative(source) + "' is unreadable; drawn as "
			"ordinary art", {}, 0, {{"path", diagnostics.Relative(source)}});
		return {};
	}

	// Uploaded through the same sink the font engine builds its pages with, so a baked sheet and a
	// glyph atlas are the same kind of resource from here on.
	const Folio::FMsdfHandle page = CreateAtlasPage(Folio::FMsdfPoint{size.x, size.y}, sidecar.Params);
	if (page == Folio::InvalidMsdfHandle)
		return {};

	UploadAtlasRegion(page, Folio::FMsdfRect{0, 0, size.x, size.y}, pixels.data(), size.x * 4);

	field_atlases_from_file.insert(Rml::TextureHandle(page));
	return Rml::TextureHandle(page);
}

Folio::FMsdfHandle PreviewRenderInterface::CreateAtlasPage(Folio::FMsdfPoint size,
	const Folio::FMsdfAtlasParams& params)
{
	if (size.X <= 0 || size.Y <= 0)
		return Folio::InvalidMsdfHandle;

	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	// Explicitly not sRGB: these channels are distances, not colour. Bilinear because the whole
	// method depends on interpolating between distances, and clamped because a field has no
	// meaningful value past its own edge.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.X, size.Y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	// Well clear of the base class's handles, which are raw GL texture names.
	const Rml::TextureHandle handle = next_msdf_handle++;
	MsdfBinding& binding = msdf_bindings[handle];
	binding.texture = texture;
	binding.unit_range = Rml::Vector2f(float(params.PxRange) / float(size.X),
		float(params.PxRange) / float(size.Y));

	return Folio::FMsdfHandle(handle);
}

void PreviewRenderInterface::UploadAtlasRegion(Folio::FMsdfHandle page, Folio::FMsdfRect region,
	const unsigned char* rgba, int stride)
{
	const auto binding = msdf_bindings.find(Rml::TextureHandle(page));
	if (binding == msdf_bindings.end() || rgba == nullptr)
		return;

	glBindTexture(GL_TEXTURE_2D, binding->second.texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, region.X, region.Y, region.Width, region.Height,
		GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

Folio::FMsdfHandle PreviewRenderInterface::CreateEffectAlias(Folio::FMsdfHandle page,
	const Folio::FMsdfEffectParams& effect)
{
	const auto source = msdf_bindings.find(Rml::TextureHandle(page));
	if (source == msdf_bindings.end())
		return Folio::InvalidMsdfHandle;

	const Rml::TextureHandle handle = next_msdf_handle++;
	MsdfBinding alias = source->second;
	alias.effect = effect;
	msdf_bindings[handle] = alias;

	return Folio::FMsdfHandle(handle);
}

void PreviewRenderInterface::ReleaseAtlasHandle(Folio::FMsdfHandle handle)
{
	const auto binding = msdf_bindings.find(Rml::TextureHandle(handle));
	if (binding == msdf_bindings.end())
		return;

	// Aliases share the page's texture, so it is only freed once nothing else names it.
	const unsigned int texture = binding->second.texture;
	msdf_bindings.erase(binding);

	for (const auto& remaining : msdf_bindings)
	{
		if (remaining.second.texture == texture)
			return;
	}

	glDeleteTextures(1, &texture);
}

Rml::CompiledFilterHandle PreviewRenderInterface::CompileFilter(const Rml::String& name,
	const Rml::Dictionary& parameters)
{
	if (name != "outside-blur")
		return RenderInterface_GL3::CompileFilter(name, parameters);

	// Compiled as an ordinary blur. What makes it "outside" happens at composite time, where the
	// blurred layer is put down in four bands around the element rather than once over it.
	const Rml::CompiledFilterHandle blur = RenderInterface_GL3::CompileFilter("blur", parameters);
	if (!blur)
		return {};

	outside_blurs[blur] = blur;
	return blur;
}

void PreviewRenderInterface::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	outside_blurs.erase(filter);
	RenderInterface_GL3::ReleaseFilter(filter);
}

void PreviewRenderInterface::EnableScissorRegion(bool enable)
{
	if (!enable)
		scissor = Rml::Rectanglei::MakeInvalid();
	RenderInterface_GL3::EnableScissorRegion(enable);
}

void PreviewRenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	scissor = region;
	RenderInterface_GL3::SetScissorRegion(region);
}

void PreviewRenderInterface::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
	Rml::BlendMode blend_mode, Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	// backdrop-filter arrives as a pair. The first call filters what is already on screen into a
	// scratch layer; the second puts that layer back, clipped to the element. So the inversion
	// belongs to the second - inverting the first would only choose which pixels get blurred, and
	// the second would still stamp the result inside the element.
	if (source < destination)
	{
		bool outside = false;
		for (const Rml::CompiledFilterHandle handle : filters)
		{
			if (outside_blurs.count(handle) != 0)
				outside = true;
		}

		pending_outside = outside;

		if (outside && viewport.x > 0 && viewport.y > 0)
		{
			// Unscissored, so the scratch layer ends up holding the blurred backdrop everywhere.
			// RmlUi has clipped this to the element, and the bands the second pass draws are all
			// outside that: left as it is, they would read pixels nothing had written.
			const Rml::Rectanglei whole = Rml::Rectanglei::FromPositionSize({0, 0}, viewport);
			RenderInterface_GL3::SetScissorRegion(whole);
			RenderInterface_GL3::CompositeLayers(source, destination, blend_mode, filters);
			RenderInterface_GL3::SetScissorRegion(scissor);
			return;
		}

		RenderInterface_GL3::CompositeLayers(source, destination, blend_mode, filters);
		return;
	}

	if (!pending_outside || !scissor.Valid() || viewport.x <= 0 || viewport.y <= 0)
	{
		pending_outside = false;
		RenderInterface_GL3::CompositeLayers(source, destination, blend_mode, filters);
		return;
	}

	pending_outside = false;

	// The element's rect is the hole; everything else takes the blurred backdrop. Four bands,
	// because a scissor is a rectangle and there is no way to cut one out of another.
	//
	//   +-----------------------+
	//   |         above         |
	//   +------+--------+-------+
	//   | left |  hole  | right |
	//   +------+--------+-------+
	//   |         below         |
	//   +-----------------------+
	//
	const Rml::Rectanglei hole = scissor;
	const int right = hole.Right();
	const int bottom = hole.Bottom();

	const Rml::Rectanglei bands[4] = {
		Rml::Rectanglei::FromPositionSize({0, 0}, {viewport.x, hole.Top()}),
		Rml::Rectanglei::FromPositionSize({0, bottom}, {viewport.x, viewport.y - bottom}),
		Rml::Rectanglei::FromPositionSize({0, hole.Top()}, {hole.Left(), hole.Height()}),
		Rml::Rectanglei::FromPositionSize({right, hole.Top()}, {viewport.x - right, hole.Height()}),
	};

	for (const Rml::Rectanglei& band : bands)
	{
		if (band.Width() <= 0 || band.Height() <= 0)
			continue;

		RenderInterface_GL3::SetScissorRegion(band);
		RenderInterface_GL3::CompositeLayers(source, destination, blend_mode, filters);
	}

	// Left as RmlUi set it, so whatever draws next is clipped to what it expects.
	RenderInterface_GL3::SetScissorRegion(hole);
}

Rml::CompiledShaderHandle PreviewRenderInterface::CompileShader(const Rml::String& name,
	const Rml::Dictionary& parameters)
{
	// `decorator: shader("...")` - the effects RCSS has no syntax for. Rasterised rather than
	// shaded, so both renderers produce the same picture from the same code.
	if (name != "shader")
		return RenderInterface_GL3::CompileShader(name, parameters);

	Rml::String value;
	if (const auto it = parameters.find("value"); it != parameters.end())
		it->second.GetInto(value);

	Rml::Vector2f dimensions(1.0f, 1.0f);
	if (const auto it = parameters.find("dimensions"); it != parameters.end())
		it->second.GetInto(dimensions);

	Folio::FShaderEffect effect;
	if (!Folio::ParseShaderEffect(value, effect))
	{
		diagnostics.Report(Severity::Warning, "W-SHADER-UNKNOWN",
			"shader('" + value + "') is not one Folio knows; it renders as nothing. Try additive, "
			"scanlines or sweep.");
		return {};
	}

	int width = 1, height = 1;
	Folio::SuggestShaderEffectSize(effect, int(dimensions.x), int(dimensions.y), width, height);

	std::vector<unsigned char> rgba;
	Folio::RasterizeShaderEffect(effect, width, height, rgba);

	const Rml::TextureHandle texture = RenderInterface_GL3::GenerateTexture(
		Rml::Span<const Rml::byte>(rgba.data(), rgba.size()), Rml::Vector2i(width, height));
	if (!texture)
		return {};

	// Scanlines are one column stretched across the box, so the horizontal wrap has to repeat or
	// the single texel smears; the vertical is the pattern itself and must tile.
	glBindTexture(GL_TEXTURE_2D, (unsigned int)texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D, 0);

	const Rml::CompiledShaderHandle handle = next_shader_effect;
	next_shader_effect += 2;
	shader_effects[handle] = ShaderEffect{effect, texture};
	return handle;
}

void PreviewRenderInterface::RenderShader(Rml::CompiledShaderHandle shader,
	Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	const auto found = shader_effects.find(shader);
	if (found == shader_effects.end())
	{
		RenderInterface_GL3::RenderShader(shader, geometry, translation, texture);
		return;
	}

	// The decorator's geometry carries normalised coordinates, so the baked texture stretches
	// over the element and this is an ordinary textured draw.
	RenderGeometry(geometry, translation, found->second.texture);
}

void PreviewRenderInterface::ReleaseShader(Rml::CompiledShaderHandle shader)
{
	const auto found = shader_effects.find(shader);
	if (found == shader_effects.end())
	{
		RenderInterface_GL3::ReleaseShader(shader);
		return;
	}

	RenderInterface_GL3::ReleaseTexture(found->second.texture);
	shader_effects.erase(found);
}

unsigned int PreviewRenderInterface::ResolveFill(const std::string& path)
{
	const auto found = fill_textures.find(path);
	if (found != fill_textures.end())
		return found->second;

	// Through our own override, because the base class reads uncompressed TGA and nothing else
	// and a pattern is a PNG like every other piece of art here.
	Rml::Vector2i size;
	Rml::TextureHandle handle = LoadTexture(size, path);

	// A field page's handle is an index, not a GL name, so binding it as a texture would sample
	// whatever shares the number.
	if (handle && msdf_bindings.find(handle) != msdf_bindings.end())
	{
		diagnostics.Report(Severity::Warning, "W-FONT-FILL",
			"'" + diagnostics.Relative(path) + "' is a distance field atlas, which cannot be a fill "
			"pattern; the text is drawn flat", {}, 0, {{"path", diagnostics.Relative(path)}});
		handle = {};
	}

	if (!handle)
	{
		diagnostics.Report(Severity::Warning, "W-FONT-FILL",
			"could not load the fill pattern '" + diagnostics.Relative(path) + "'; the text is drawn flat",
			{}, 0, {{"path", diagnostics.Relative(path)}});
	}
	else
	{
		// Tiled, since the pattern is anchored to the screen and has to cover it.
		glBindTexture(GL_TEXTURE_2D, (unsigned int)handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	fill_textures[path] = (unsigned int)handle;
	return (unsigned int)handle;
}

void PreviewRenderInterface::ClearPlaceholders()
{
	for (const auto& [name, handle] : placeholder_textures)
		RenderInterface_GL3::ReleaseTexture(handle);

	placeholder_textures.clear();
	placeholder_names.clear();
}

} // namespace uipreview

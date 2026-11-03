#include "FilterProgram.h"

#include "GL.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uipreview {

namespace {

#ifdef __EMSCRIPTEN__
const char* const version_header = "#version 300 es\nprecision highp float;\n";
#else
const char* const version_header = "#version 330\n";
#endif

// A triangle that covers the target, with no vertex buffer: positions come from the vertex index.
const char* const fullscreen_vert = R"(
void main()
{
	vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const copy_frag = R"(
uniform sampler2D _source;
uniform vec2 _size;
out vec4 finalColor;
void main() { finalColor = texture(_source, gl_FragCoord.xy / _size); }
)";

// Mode 0 writes the tinted coverage, offset. Mode 1 puts the blurred halo behind the source.
const char* const halo_frag = R"(
uniform sampler2D _source;
uniform sampler2D _halo;
uniform vec2 _size;
uniform vec4 _colour;
uniform vec2 _offset;
uniform float _strength;
uniform int _mode;
uniform vec4 _limits;
out vec4 finalColor;
void main()
{
	vec2 uv = gl_FragCoord.xy / _size;
	if (_mode == 0)
	{
		vec2 from = uv - _offset;
		bool inside = all(greaterThanEqual(from, _limits.xy)) && all(lessThanEqual(from, _limits.zw));
		finalColor = _colour * (inside ? texture(_source, from).a : 0.0);
		return;
	}
	vec4 halo = texture(_halo, uv);
	float alpha = clamp(halo.a * _strength, 0.0, 1.0);
	vec4 strengthened = halo.a > 0.0 ? halo * (alpha / halo.a) : vec4(0.0);
	vec4 source = texture(_source, uv);
	finalColor = source + strengthened * (1.0 - source.a);
}
)";

const char* const blur_frag = R"(
uniform sampler2D _source;
uniform vec2 _size;
uniform vec2 _direction;
uniform float _sigma;
out vec4 finalColor;
void main()
{
	vec2 uv = gl_FragCoord.xy / _size;
	int taps = int(clamp(ceil(_sigma * 3.0), 1.0, 64.0));
	vec4 sum = texture(_source, uv);
	float weight = 1.0;
	for (int i = 1; i <= taps; ++i)
	{
		float w = exp(-0.5 * float(i * i) / (_sigma * _sigma));
		vec2 step = _direction * float(i) / _size;
		sum += w * (texture(_source, uv + step) + texture(_source, uv - step));
		weight += 2.0 * w;
	}
	finalColor = sum / weight;
}
)";

// What every preview shader is compiled after. The engine's convention is a top-left origin, so the
// helpers flip GL's: a pass written against the engine reads the same pixels here.
const char* const shader_prelude = R"(
uniform sampler2D folio_source_texture;
uniform vec2 folio_size;
uniform vec4 folio_args[16];
uniform int folio_arg_count;
uniform float folio_time;
uniform float folio_pixel_scale;
out vec4 folio_colour;
vec2 folio_uv() { vec2 uv = gl_FragCoord.xy / folio_size; return vec2(uv.x, 1.0 - uv.y); }
vec4 folio_source(vec2 uv) { return texture(folio_source_texture, vec2(uv.x, 1.0 - uv.y)); }
#line 1
)";

GLuint Compile(GLenum type, const char* prelude, const char* body, std::string& error)
{
	const GLuint shader = glCreateShader(type);
	const char* const sources[] = {version_header, prelude, body};
	glShaderSource(shader, 3, sources, nullptr);
	glCompileShader(shader);

	GLint status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_TRUE)
		return shader;

	GLint length = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
	std::vector<char> log((size_t)std::max(length, 1));
	glGetShaderInfoLog(shader, length, nullptr, log.data());
	error = log.data();
	glDeleteShader(shader);
	return 0;
}

GLuint Link(const char* prelude, const char* fragment, std::string& error)
{
	const GLuint vertex = Compile(GL_VERTEX_SHADER, "", fullscreen_vert, error);
	if (!vertex)
		return 0;

	const GLuint pixel = Compile(GL_FRAGMENT_SHADER, prelude, fragment, error);
	if (!pixel)
	{
		glDeleteShader(vertex);
		return 0;
	}

	const GLuint program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, pixel);
	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(pixel);

	GLint status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_TRUE)
		return program;

	GLint length = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
	std::vector<char> log((size_t)std::max(length, 1));
	glGetProgramInfoLog(program, length, nullptr, log.data());
	error = log.data();
	glDeleteProgram(program);
	return 0;
}

void BindSource(GLuint program, const char* name, int unit, GLuint texture)
{
	glActiveTexture(GLenum(GL_TEXTURE0 + unit));
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(glGetUniformLocation(program, name), unit);
}

GLuint BoundFramebuffer()
{
	GLint bound = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
	return GLuint(bound);
}

} // namespace

FilterProgram::~FilterProgram()
{
	for (const GLuint program : {halo_program, blur_program, copy_program})
	{
		if (program)
			glDeleteProgram(program);
	}
	for (const auto& entry : shaders)
	{
		if (entry.second.id)
			glDeleteProgram(entry.second.id);
	}
	glDeleteFramebuffers(2, framebuffers);
	glDeleteTextures(2, textures);
	if (vao)
		glDeleteVertexArrays(1, &vao);
}

bool FilterProgram::EnsureBuiltIns()
{
	if (copy_program)
		return true;
	if (built_ins_failed)
		return false;

	std::string error;
	copy_program = Link("", copy_frag, error);
	halo_program = copy_program ? Link("", halo_frag, error) : 0;
	blur_program = halo_program ? Link("", blur_frag, error) : 0;
	if (!blur_program)
	{
		built_ins_failed = true;
		Rml::Log::Message(Rml::Log::LT_ERROR, "Folio's filter programs did not compile: %s", error.c_str());
		return false;
	}

	glGenVertexArrays(1, &vao);
	return true;
}

bool FilterProgram::EnsureTargets(int width, int height)
{
	if (width == target_width && height == target_height && textures[0])
		return true;

	if (!textures[0])
	{
		glGenTextures(2, textures);
		glGenFramebuffers(2, framebuffers);
	}

	for (int index = 0; index < 2; ++index)
	{
		glBindTexture(GL_TEXTURE_2D, textures[index]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[index]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[index], 0);
	}

	target_width = width;
	target_height = height;
	return true;
}

void FilterProgram::Blur(unsigned int texture, unsigned int framebuffer, float sigma, int width, int height)
{
	if (sigma < 0.5f)
		return;

	// The other scratch target holds the horizontal half.
	const int other = texture == textures[0] ? 1 : 0;

	glUseProgram(blur_program);
	glUniform2f(glGetUniformLocation(blur_program, "_size"), float(width), float(height));
	glUniform1f(glGetUniformLocation(blur_program, "_sigma"), sigma);

	glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[other]);
	BindSource(blur_program, "_source", 0, texture);
	glUniform2f(glGetUniformLocation(blur_program, "_direction"), 1.0f, 0.0f);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	BindSource(blur_program, "_source", 0, textures[other]);
	glUniform2f(glGetUniformLocation(blur_program, "_direction"), 0.0f, 1.0f);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

void FilterProgram::DrawHalo(unsigned int source, int width, int height, const float colour[4], float offset_x,
	float offset_y, float sigma, float strength)
{
	const GLuint destination = BoundFramebuffer();
	if (!EnsureBuiltIns() || !EnsureTargets(width, height))
		return;

	glBindVertexArray(vao);

	// The scratch targets are drawn whole: the blur reads past the scissor, and outside it they would still hold
	// whatever the last filter left there.
	const bool scissored = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
	glDisable(GL_SCISSOR_TEST);

	// Only the scissored part of the layer was written this frame, so only it counts as coverage.
	float limits[4] = {0.0f, 0.0f, 1.0f, 1.0f};
	if (scissored)
	{
		GLint box[4] = {0, 0, width, height};
		glGetIntegerv(GL_SCISSOR_BOX, box);
		limits[0] = float(box[0]) / float(width);
		limits[1] = float(box[1]) / float(height);
		limits[2] = float(box[0] + box[2]) / float(width);
		limits[3] = float(box[1] + box[3]) / float(height);
	}

	// GL's origin is the bottom left, so a shadow moving down the document moves up the texture.
	glUseProgram(halo_program);
	glUniform2f(glGetUniformLocation(halo_program, "_size"), float(width), float(height));
	glUniform4fv(glGetUniformLocation(halo_program, "_colour"), 1, colour);
	glUniform2f(glGetUniformLocation(halo_program, "_offset"), offset_x / float(width), -offset_y / float(height));
	glUniform1f(glGetUniformLocation(halo_program, "_strength"), strength);
	glUniform1i(glGetUniformLocation(halo_program, "_mode"), 0);
	glUniform4fv(glGetUniformLocation(halo_program, "_limits"), 1, limits);
	BindSource(halo_program, "_source", 0, source);
	BindSource(halo_program, "_halo", 1, source);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	Blur(textures[0], framebuffers[0], sigma, width, height);

	if (scissored)
		glEnable(GL_SCISSOR_TEST);

	glUseProgram(halo_program);
	glUniform1i(glGetUniformLocation(halo_program, "_mode"), 1);
	BindSource(halo_program, "_source", 0, source);
	BindSource(halo_program, "_halo", 1, textures[0]);
	glBindFramebuffer(GL_FRAMEBUFFER, destination);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

const FilterProgram::Program& FilterProgram::ShaderFor(const std::string& name)
{
	const auto found = shaders.find(name);
	if (found != shaders.end())
		return found->second;

	Program& program = shaders[name];

	// The project's own first, then any Folio ships for its examples.
	std::string text;
	std::string path;
	for (const std::string& candidate : {"UI/filters/" + name + ".frag", "folio/examples/filters/" + name + ".frag"})
	{
		if (Rml::GetFileInterface()->LoadFile(candidate, text))
		{
			path = candidate;
			break;
		}
	}

	if (path.empty())
	{
		Rml::Log::Message(Rml::Log::LT_WARNING,
			"shader filter '%s' has no preview program, so it draws unfiltered here. Add UI/filters/%s.frag to preview it.",
			name.c_str(), name.c_str());
		return program;
	}

	std::string error;
	program.id = Link(shader_prelude, text.c_str(), error);
	if (!program.id)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "%s did not compile: %s", path.c_str(), error.c_str());
		return program;
	}

	program.animated = glGetUniformLocation(program.id, "folio_time") >= 0;
	return program;
}

bool FilterProgram::DrawShader(const std::string& name, const std::vector<Folio::FShaderFilterArg>& args,
	unsigned int source, int width, int height, float time, float pixel_scale)
{
	if (!EnsureBuiltIns())
		return false;

	glBindVertexArray(vao);

	const Program& program = ShaderFor(name);
	if (!program.id)
	{
		glUseProgram(copy_program);
		glUniform2f(glGetUniformLocation(copy_program, "_size"), float(width), float(height));
		BindSource(copy_program, "_source", 0, source);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		return false;
	}

	float packed[16 * 4] = {};
	const int count = int(std::min<size_t>(args.size(), 16));
	for (int index = 0; index < count; ++index)
	{
		const Folio::FShaderFilterArg& arg = args[size_t(index)];
		float* slot = packed + index * 4;
		if (arg.Kind == Folio::FShaderFilterArg::EKind::Colour)
			std::memcpy(slot, arg.Colour, sizeof(arg.Colour));
		else
			slot[0] = arg.Value;
	}

	glUseProgram(program.id);
	glUniform2f(glGetUniformLocation(program.id, "folio_size"), float(width), float(height));
	glUniform4fv(glGetUniformLocation(program.id, "folio_args"), 16, packed);
	glUniform1i(glGetUniformLocation(program.id, "folio_arg_count"), count);
	glUniform1f(glGetUniformLocation(program.id, "folio_time"), time);
	glUniform1f(glGetUniformLocation(program.id, "folio_pixel_scale"), pixel_scale);
	BindSource(program.id, "folio_source_texture", 0, source);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	return program.animated;
}

} // namespace uipreview

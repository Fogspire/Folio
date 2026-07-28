#include "MsdfProgram.h"

#include "RmlUi_Include_GL3.h"

#include <string>
#include <vector>

namespace uipreview {

namespace {

const char* const shader_vert = R"(#version 330
uniform vec2 _translate;
uniform mat4 _transform;

in vec2 inPosition;
in vec4 inColor0;
in vec2 inTexCoord0;

out vec2 fragTexCoord;
out vec4 fragColor;

void main() {
	fragTexCoord = inTexCoord0;
	fragColor = inColor0;
	gl_Position = _transform * vec4(inPosition + _translate, 0.0, 1.0);
}
)";

// FOLIO_MSDF_GLSL_BEGIN
// Translation of Shaders/Private/FolioMsdf.ush. Keep the two in step; the parity script diffs
// them after a fixed token mapping, and it is the only thing that catches drift, because the two
// renderers' colour pipelines differ enough that a screenshot comparison never would.
const char* const shader_frag = R"(#version 330
uniform sampler2D _atlas;
uniform vec2 _unitRange;
uniform float _threshold;
uniform float _mode;
uniform float _offset;
uniform float _softness;

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

float FolioMsdfMedian(vec3 Field) {
	return max(min(Field.r, Field.g), min(max(Field.r, Field.g), Field.b));
}

void main() {
	vec4 Sample = texture(_atlas, fragTexCoord);
	float MedianDistance = FolioMsdfMedian(Sample.rgb);
	float TrueDistance = Sample.a;

	float Spread = max(dot(_unitRange, 1.0 / fwidth(fragTexCoord)), 1.0);

	float Coverage;
	if (_mode < 0.5) {
		Coverage = clamp((MedianDistance - _threshold) * Spread + 0.5, 0.0, 1.0);
	} else if (_mode < 1.5) {
		Coverage = clamp((TrueDistance - (_threshold - _offset)) * Spread + 0.5, 0.0, 1.0);
	} else {
		Coverage = smoothstep(_threshold - _offset - _softness, _threshold - _offset + _softness, TrueDistance);
	}

	finalColor = fragColor * Coverage;
}
)";
// FOLIO_MSDF_GLSL_END

bool CompileStage(GLenum type, const char* source, GLuint& out_shader, std::string& error)
{
	out_shader = glCreateShader(type);
	glShaderSource(out_shader, 1, &source, nullptr);
	glCompileShader(out_shader);

	GLint status = GL_FALSE;
	glGetShaderiv(out_shader, GL_COMPILE_STATUS, &status);
	if (status == GL_TRUE)
		return true;

	GLint length = 0;
	glGetShaderiv(out_shader, GL_INFO_LOG_LENGTH, &length);
	std::vector<char> log((size_t)std::max(length, 1));
	glGetShaderInfoLog(out_shader, length, nullptr, log.data());
	error = log.data();

	glDeleteShader(out_shader);
	out_shader = 0;
	return false;
}

} // namespace

MsdfProgram::~MsdfProgram()
{
	if (program != 0)
		glDeleteProgram(program);
}

bool MsdfProgram::Initialise(std::string& error)
{
	GLuint vertex = 0, fragment = 0;
	if (!CompileStage(GL_VERTEX_SHADER, shader_vert, vertex, error))
		return false;
	if (!CompileStage(GL_FRAGMENT_SHADER, shader_frag, fragment, error))
	{
		glDeleteShader(vertex);
		return false;
	}

	program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);

	// Same indices the base backend binds, so its vertex layout describes our buffers too.
	glBindAttribLocation(program, 0, "inPosition");
	glBindAttribLocation(program, 1, "inColor0");
	glBindAttribLocation(program, 2, "inTexCoord0");

	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);

	GLint status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status != GL_TRUE)
	{
		GLint length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log((size_t)std::max(length, 1));
		glGetProgramInfoLog(program, length, nullptr, log.data());
		error = log.data();

		glDeleteProgram(program);
		program = 0;
		return false;
	}

	uniform_translate = glGetUniformLocation(program, "_translate");
	uniform_transform = glGetUniformLocation(program, "_transform");
	uniform_atlas = glGetUniformLocation(program, "_atlas");
	uniform_unit_range = glGetUniformLocation(program, "_unitRange");
	uniform_threshold = glGetUniformLocation(program, "_threshold");
	uniform_mode = glGetUniformLocation(program, "_mode");
	uniform_offset = glGetUniformLocation(program, "_offset");
	uniform_softness = glGetUniformLocation(program, "_softness");

	return true;
}

void MsdfProgram::Bind(const Rml::Matrix4f& transform, Rml::Vector2f translation, unsigned int texture,
	Rml::Vector2f unit_range, const Folio::FMsdfEffectParams& effect)
{
	glUseProgram(program);

	glUniformMatrix4fv(uniform_transform, 1, GL_FALSE, transform.data());
	glUniform2fv(uniform_translate, 1, &translation.x);
	glUniform2fv(uniform_unit_range, 1, &unit_range.x);
	glUniform1f(uniform_threshold, 0.5f);
	glUniform1f(uniform_mode, float((int)effect.Effect));
	glUniform1f(uniform_offset, effect.OffsetEm);
	glUniform1f(uniform_softness, effect.SoftnessEm);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(uniform_atlas, 0);
}

void MsdfGeometry::Upload(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	if (vao == 0)
	{
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ibo);
	}

	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Rml::Vertex)), vertices.data(), GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), (const GLvoid*)offsetof(Rml::Vertex, position));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex), (const GLvoid*)offsetof(Rml::Vertex, colour));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex), (const GLvoid*)offsetof(Rml::Vertex, tex_coord));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(int)), indices.data(), GL_STATIC_DRAW);

	glBindVertexArray(0);
	index_count = int(indices.size());
}

void MsdfGeometry::Release()
{
	if (vao != 0)
	{
		glDeleteVertexArrays(1, &vao);
		glDeleteBuffers(1, &vbo);
		glDeleteBuffers(1, &ibo);
		vao = vbo = ibo = 0;
		index_count = 0;
	}
}

} // namespace uipreview

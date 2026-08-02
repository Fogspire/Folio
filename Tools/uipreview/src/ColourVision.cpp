#include "ColourVision.h"

#include <algorithm>
#include <cmath>

namespace uipreview {

namespace {

struct Matrix {
	float m[9];
};

// Machado, Oliveira and Fernandes 2009, severity 1.0, in linear RGB.
constexpr Matrix kProtanopia = {{
	0.152286f, 1.052583f, -0.204868f,
	0.114503f, 0.786281f, 0.099216f,
	-0.003882f, -0.048116f, 1.051998f,
}};

constexpr Matrix kDeuteranopia = {{
	0.367322f, 0.860646f, -0.227968f,
	0.280085f, 0.672501f, 0.047413f,
	-0.011820f, 0.042940f, 0.968881f,
}};

constexpr Matrix kTritanopia = {{
	1.255528f, -0.076749f, -0.178779f,
	-0.078411f, 0.930809f, 0.147602f,
	0.004733f, 0.691367f, 0.303900f,
}};

// The luminance weights, so every channel comes out the same grey. Not one of Machado's.
constexpr Matrix kAchromatopsia = {{
	0.2126f, 0.7152f, 0.0722f,
	0.2126f, 0.7152f, 0.0722f,
	0.2126f, 0.7152f, 0.0722f,
}};

const Matrix* MatrixFor(ColourVision vision)
{
	switch (vision)
	{
	case ColourVision::Protanopia: return &kProtanopia;
	case ColourVision::Deuteranopia: return &kDeuteranopia;
	case ColourVision::Tritanopia: return &kTritanopia;
	case ColourVision::Achromatopsia: return &kAchromatopsia;
	default: return nullptr;
	}
}

// The sRGB transfer, not a gamma of 2.2. A matrix meant for linear light applied to encoded values
// shifts every mid-tone, which is exactly where the colours a UI uses live.
float ToLinear(unsigned char value)
{
	const float v = value / 255.0f;
	return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

unsigned char ToEncoded(float linear)
{
	const float clamped = std::clamp(linear, 0.0f, 1.0f);
	const float v = clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
	return (unsigned char)std::lround(v * 255.0f);
}

void Apply(const Matrix& matrix, unsigned char& r, unsigned char& g, unsigned char& b)
{
	const float lr = ToLinear(r);
	const float lg = ToLinear(g);
	const float lb = ToLinear(b);

	r = ToEncoded(matrix.m[0] * lr + matrix.m[1] * lg + matrix.m[2] * lb);
	g = ToEncoded(matrix.m[3] * lr + matrix.m[4] * lg + matrix.m[5] * lb);
	b = ToEncoded(matrix.m[6] * lr + matrix.m[7] * lg + matrix.m[8] * lb);
}

} // namespace

bool ParseColourVision(const std::string& name, ColourVision& out_vision)
{
	if (name == "none" || name == "normal") { out_vision = ColourVision::Normal; return true; }
	if (name == "protanopia" || name == "protan") { out_vision = ColourVision::Protanopia; return true; }
	if (name == "deuteranopia" || name == "deutan") { out_vision = ColourVision::Deuteranopia; return true; }
	if (name == "tritanopia" || name == "tritan") { out_vision = ColourVision::Tritanopia; return true; }
	if (name == "achromatopsia" || name == "mono") { out_vision = ColourVision::Achromatopsia; return true; }

	return false;
}

const char* ColourVisionName(ColourVision vision)
{
	switch (vision)
	{
	case ColourVision::Protanopia: return "protanopia";
	case ColourVision::Deuteranopia: return "deuteranopia";
	case ColourVision::Tritanopia: return "tritanopia";
	case ColourVision::Achromatopsia: return "achromatopsia";
	default: return "normal";
	}
}

Rml::Colourb SimulateColour(Rml::Colourb colour, ColourVision vision)
{
	const Matrix* matrix = MatrixFor(vision);
	if (!matrix)
		return colour;

	Apply(*matrix, colour.red, colour.green, colour.blue);
	return colour;
}

void SimulateImage(std::vector<unsigned char>& rgba, ColourVision vision)
{
	const Matrix* matrix = MatrixFor(vision);
	if (!matrix)
		return;

	for (size_t i = 0; i + 3 < rgba.size(); i += 4)
		Apply(*matrix, rgba[i], rgba[i + 1], rgba[i + 2]);
}

} // namespace uipreview

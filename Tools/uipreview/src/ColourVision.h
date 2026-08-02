#pragma once

#include <RmlUi/Core/Types.h>

#include <string>
#include <vector>

namespace uipreview {

/**
 * What a screen looks like to someone whose colour vision is not the one it was designed against.
 *
 * Around one man in twelve has some form of it, which makes it the single most common visual
 * difference a UI meets - far more common than low vision and orders of magnitude more common than
 * blindness. It is also the one nobody catches by looking, because the person who drew the screen sees
 * it working.
 *
 * The transforms are Machado, Oliveira and Fernandes (2009) at full severity, which operate on linear
 * RGB and are the reference implementation everything else quotes. Dichromacy only: an anomalous
 * trichromat at partial severity is a family of curves rather than one, and simulating the severe end
 * is what tells you whether a screen survives.
 */
enum class ColourVision {
	Normal,
	/** No long-wavelength cone. Red darkens towards black; red and green converge on yellow. */
	Protanopia,
	/** No medium-wavelength cone. The commonest, and the one red-on-green status colours fail. */
	Deuteranopia,
	/** No short-wavelength cone. Rare. Blue and green converge; yellow and violet converge. */
	Tritanopia,
	/** No colour at all. Not a real check for most games, and the fastest way to see what a palette
	    is doing with luminance alone. */
	Achromatopsia,
};

/** Parses a name for --simulate. False on anything else, with the caller reporting it. */
bool ParseColourVision(const std::string& name, ColourVision& out_vision);

const char* ColourVisionName(ColourVision vision);

/** One colour as that vision sees it. Alpha is carried through untouched. */
Rml::Colourb SimulateColour(Rml::Colourb colour, ColourVision vision);

/** A whole RGBA image in place, for a screenshot someone is going to look at. */
void SimulateImage(std::vector<unsigned char>& rgba, ColourVision vision);

} // namespace uipreview

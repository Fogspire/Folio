#pragma once

#include "FolioRmlUi.h"

/**
 * Gradient decorators, baked into textures.
 *
 * RmlUi expresses every gradient as a compiled shader, which normally means a fragment shader
 * per gradient type. Slate has no seam for that through MakeCustomVerts, and it does not need
 * one: a gradient is a pure function of position, so evaluating it once into a small texture
 * and sampling that gives the same picture with no shader plumbing at all.
 *
 * The geometry's texture coordinates arrive as pixel offsets inside the element's box rather
 * than as normalised UVs, which is what makes this work: the same coordinate space the shader
 * parameters are expressed in.
 */
namespace Folio
{
	/** The parameters RmlUi hands to CompileShader, in the form the evaluator needs. */
	struct FGradient
	{
		enum class EType : uint8 { Linear, Radial, Conic };

		EType Type = EType::Linear;
		bool bRepeating = false;

		/** Linear: the gradient line. Radial and conic: centre, radius or angle. */
		FVector2f P0 = FVector2f::ZeroVector;
		FVector2f P1 = FVector2f::ZeroVector;
		FVector2f Centre = FVector2f::ZeroVector;
		FVector2f Radius = FVector2f(1.0f, 1.0f);
		float Length = 1.0f;
		float Angle = 0.0f;

		TArray<FColor> StopColours;
		TArray<float> StopPositions;

		bool IsValid() const { return StopColours.Num() >= 1; }
	};

	/** Reads a gradient out of the dictionary RmlUi passes to CompileShader. */
	bool ParseGradient(const Rml::String& Name, const Rml::Dictionary& Parameters, FGradient& OutGradient);

	/**
	 * Evaluates the gradient over a box of the given size into premultiplied BGRA.
	 *
	 * The size is the resolution of the generated texture, not the element: a gradient is
	 * smooth, so a modest texture stretched over a large box is indistinguishable from one
	 * evaluated per pixel, and it keeps a full-screen background from allocating a
	 * full-screen texture.
	 */
	void RasterizeGradient(const FGradient& Gradient, FVector2f BoxSize, FIntPoint TextureSize,
		TArray<uint8>& OutBgra);
}

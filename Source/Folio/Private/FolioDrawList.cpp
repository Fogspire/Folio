#include "FolioDrawList.h"

#include "Engine/Texture.h"

namespace
{
	FLinearColor ToLinear(const FColor& Color, const bool bDecodeSrgb)
	{
		// The target is sRGB, so the shader's output is encoded on write. Passing the byte values through
		// as though they were linear encodes them a second time, which turns a #1f1f28 panel into
		// (98,98,110) - exactly LinearToSRGB(31/255) * 255. Decoding first makes the round trip land back
		// on the original value.
		//
		// The real sRGB curve, not a pow(2.2) approximation: the curve has a linear toe, and
		// approximating it lands #1f1f28 on (22,22,34) instead of (31,31,40). UI colours are dark, which
		// is precisely where the two disagree most.
		// Not decoded when the destination is already encoded, which is the case after the tonemapper.
		FLinearColor Linear = bDecodeSrgb
			? FLinearColor::FromSRGBColor(Color)
			: FLinearColor(Color.R / 255.0f, Color.G / 255.0f, Color.B / 255.0f, 1.0f);

		// Alpha is coverage, not a colour, and must not be decoded.
		Linear.A = Color.A / 255.0f;
		return Linear;
	}
}

void FFolioDrawList::Convert(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	const FVector2f Offset, const bool bUnpremultiply, const bool bDecodeSrgb, FFolioDraw& OutDraw)
{
	OutDraw.Vertices.Reserve(Vertices.Num());
	for (const FSlateVertex& Vertex : Vertices)
	{
		FColor Color = Vertex.Color;

		// ToLinear decodes sRGB, which is only meaningful for a straight colour: a premultiplied one
		// raises the product to a power rather than the colour, and every partially covered pixel comes
		// out too dark. Harmless for an image, which is opaque wherever it matters, and a dark rim
		// around every glyph where it is not.
		if (bUnpremultiply && Color.A != 0 && Color.A != 255)
		{
			const float Alpha = float(Color.A) / 255.0f;
			Color.R = uint8(FMath::Clamp(FMath::RoundToInt(float(Color.R) / Alpha), 0, 255));
			Color.G = uint8(FMath::Clamp(FMath::RoundToInt(float(Color.G) / Alpha), 0, 255));
			Color.B = uint8(FMath::Clamp(FMath::RoundToInt(float(Color.B) / Alpha), 0, 255));
		}

		FFolioUIVertex& Out = OutDraw.Vertices.AddDefaulted_GetRef();
		Out.Position = FVector2f(Vertex.Position) + Offset;
		Out.UV = FVector2f(Vertex.TexCoords[0], Vertex.TexCoords[1]);
		Out.Color = ToLinear(Color, bDecodeSrgb);
	}

	OutDraw.Indices.Reserve(Indices.Num());
	for (const SlateIndex Index : Indices)
	{
		OutDraw.Indices.Add(static_cast<uint32>(Index));
	}
}

void FFolioDrawList::AddTriangles(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset, const bool bMasked, const bool bMaskInverted,
	const bool bTiles, const bool bStraightAlpha)
{
	if (Indices.Num() < 3 || !Texture)
	{
		return;
	}

	FFolioDraw Draw;
	Draw.Kind = FFolioDraw::EKind::Geometry;
	Draw.Texture.Reset(Texture);
	Draw.bMasked = bMasked;
	Draw.bMaskInverted = bMaskInverted;
	Draw.bTiles = bTiles;
	Draw.bStraightAlpha = bStraightAlpha;
	Convert(Vertices, Indices, Offset, /*bUnpremultiply*/ false, bDecodeSrgb, Draw);

	Draw.Scissor = Scissor;
	Draws.Add(MoveTemp(Draw));
}

void FFolioDrawList::AddField(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset, const FVector4f Field, const FVector4f Effect,
	const bool bMasked, const bool bMaskInverted)
{
	AddFieldAt(Vertices, Indices, Texture, Offset, Field, Effect, bMasked, bMaskInverted, Scissor);
}

void FFolioDrawList::AddFieldAt(const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices,
	UTexture* Texture, const FVector2f Offset, const FVector4f Field, const FVector4f Effect,
	const bool bMasked, const bool bMaskInverted, const FIntRect& InScissor)
{
	if (Indices.Num() < 3 || !Texture)
	{
		return;
	}

	FFolioDraw Draw;
	Draw.Kind = FFolioDraw::EKind::Field;
	Draw.Texture.Reset(Texture);
	Draw.bMasked = bMasked;
	Draw.bMaskInverted = bMaskInverted;
	Draw.Field = Field;
	Draw.Effect = Effect;
	Convert(Vertices, Indices, Offset, /*bUnpremultiply*/ true, bDecodeSrgb, Draw);

	Draw.Scissor = InScissor;
	Draws.Add(MoveTemp(Draw));
}

void FFolioDrawList::AddClipMask(const Rml::ClipMaskOperation Operation,
	const TArray<FSlateVertex>& Vertices, const TArray<SlateIndex>& Indices, const FVector2f Offset)
{
	if (Indices.Num() < 3)
	{
		return;
	}

	FFolioDraw Draw;
	switch (Operation)
	{
	case Rml::ClipMaskOperation::Set:
		Draw.Kind = FFolioDraw::EKind::MaskSet;
		break;

	case Rml::ClipMaskOperation::SetInverse:
		// The whole reason this path draws with RHI. An outer box-shadow masks its own element out of
		// itself, and only a stencil can express the complement of a shape.
		Draw.Kind = FFolioDraw::EKind::MaskSetInverse;
		break;

	case Rml::ClipMaskOperation::Intersect:
		Draw.Kind = FFolioDraw::EKind::MaskIntersect;
		break;
	}

	Convert(Vertices, Indices, Offset, /*bUnpremultiply*/ false, bDecodeSrgb, Draw);
	Draw.Scissor = Scissor;
	Draws.Add(MoveTemp(Draw));
}

int32 FFolioDrawList::CountTriangles() const
{
	int32 Total = 0;
	for (const FFolioDraw& Draw : Draws)
	{
		if (Draw.Kind == FFolioDraw::EKind::Geometry || Draw.Kind == FFolioDraw::EKind::Field)
		{
			Total += Draw.Indices.Num() / 3;
		}
	}
	return Total;
}

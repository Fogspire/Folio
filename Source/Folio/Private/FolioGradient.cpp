#include "FolioGradient.h"

#include "FolioModule.h"

namespace
{
	const Rml::Variant* Find(const Rml::Dictionary& Parameters, const char* Key)
	{
		const auto It = Parameters.find(Key);
		return It != Parameters.end() ? &It->second : nullptr;
	}

	template <typename T>
	bool Read(const Rml::Dictionary& Parameters, const char* Key, T& OutValue)
	{
		const Rml::Variant* Variant = Find(Parameters, Key);
		return Variant && Variant->GetInto(OutValue);
	}

	FVector2f ReadVector(const Rml::Dictionary& Parameters, const char* Key, const FVector2f Fallback)
	{
		Rml::Vector2f Value;
		return Read(Parameters, Key, Value) ? FVector2f(Value.x, Value.y) : Fallback;
	}
}

namespace Folio
{
	bool ParseGradient(const Rml::String& Name, const Rml::Dictionary& Parameters, FGradient& OutGradient)
	{
		if (Name == "linear-gradient")
		{
			OutGradient.Type = FGradient::EType::Linear;
			OutGradient.P0 = ReadVector(Parameters, "p0", FVector2f::ZeroVector);
			OutGradient.P1 = ReadVector(Parameters, "p1", FVector2f(1.0f, 0.0f));
			Read(Parameters, "length", OutGradient.Length);
		}
		else if (Name == "radial-gradient")
		{
			OutGradient.Type = FGradient::EType::Radial;
			OutGradient.Centre = ReadVector(Parameters, "center", FVector2f::ZeroVector);
			OutGradient.Radius = ReadVector(Parameters, "radius", FVector2f(1.0f, 1.0f));
		}
		else if (Name == "conic-gradient")
		{
			OutGradient.Type = FGradient::EType::Conic;
			OutGradient.Centre = ReadVector(Parameters, "center", FVector2f::ZeroVector);
			Read(Parameters, "angle", OutGradient.Angle);
		}
		else
		{
			return false;
		}

		Read(Parameters, "repeating", OutGradient.bRepeating);

		const Rml::Variant* Stops = Find(Parameters, "color_stop_list");
		if (!Stops)
		{
			return false;
		}

		Rml::ColorStopList StopList;
		if (!Stops->GetInto(StopList) || StopList.empty())
		{
			return false;
		}

		OutGradient.StopColours.Reserve(static_cast<int32>(StopList.size()));
		OutGradient.StopPositions.Reserve(static_cast<int32>(StopList.size()));
		for (const Rml::ColorStop& Stop : StopList)
		{
			OutGradient.StopColours.Add(FColor(Stop.color.red, Stop.color.green, Stop.color.blue, Stop.color.alpha));

			// Positions are resolved to the gradient's own space by the decorator before it
			// reaches here, so the number is already what the evaluator wants.
			OutGradient.StopPositions.Add(Stop.position.number);
		}

		return OutGradient.IsValid();
	}

	namespace
	{
		FColor SampleStops(const FGradient& Gradient, float T)
		{
			const int32 Count = Gradient.StopColours.Num();
			if (Count == 1)
			{
				return Gradient.StopColours[0];
			}

			const float First = Gradient.StopPositions[0];
			const float Last = Gradient.StopPositions[Count - 1];
			const float Span = Last - First;

			if (Gradient.bRepeating && Span > KINDA_SMALL_NUMBER)
			{
				T = First + FMath::Fmod(FMath::Fmod(T - First, Span) + Span, Span);
			}

			if (T <= First) { return Gradient.StopColours[0]; }
			if (T >= Last)  { return Gradient.StopColours[Count - 1]; }

			for (int32 Index = 1; Index < Count; ++Index)
			{
				const float Position = Gradient.StopPositions[Index];
				if (T > Position)
				{
					continue;
				}

				const float Previous = Gradient.StopPositions[Index - 1];
				const float Range = Position - Previous;
				const float Alpha = Range > KINDA_SMALL_NUMBER ? (T - Previous) / Range : 0.0f;

				// Premultiplied colours interpolate linearly, which is the whole reason
				// RmlUi hands them over premultiplied.
				const FColor& A = Gradient.StopColours[Index - 1];
				const FColor& B = Gradient.StopColours[Index];
				return FColor(
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp<float>(A.R, B.R, Alpha))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp<float>(A.G, B.G, Alpha))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp<float>(A.B, B.B, Alpha))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp<float>(A.A, B.A, Alpha))));
			}

			return Gradient.StopColours[Count - 1];
		}

		float EvaluateT(const FGradient& Gradient, const FVector2f Point)
		{
			switch (Gradient.Type)
			{
			case FGradient::EType::Linear:
			{
				const FVector2f Axis = Gradient.P1 - Gradient.P0;
				const float LengthSquared = Axis.SizeSquared();
				if (LengthSquared <= KINDA_SMALL_NUMBER)
				{
					return 0.0f;
				}
				return FVector2f::DotProduct(Point - Gradient.P0, Axis) / LengthSquared;
			}

			case FGradient::EType::Radial:
			{
				const FVector2f Radius(
					FMath::Max(Gradient.Radius.X, KINDA_SMALL_NUMBER),
					FMath::Max(Gradient.Radius.Y, KINDA_SMALL_NUMBER));
				const FVector2f Offset = (Point - Gradient.Centre) / Radius;
				return Offset.Size();
			}

			case FGradient::EType::Conic:
			{
				const FVector2f Offset = Point - Gradient.Centre;

				// Zero is straight up and the sweep is clockwise, matching CSS rather than
				// atan2's mathematical convention.
				float Angle = FMath::Atan2(Offset.X, -Offset.Y) - Gradient.Angle;
				Angle = FMath::Fmod(FMath::Fmod(Angle, UE_TWO_PI) + UE_TWO_PI, UE_TWO_PI);
				return Angle / UE_TWO_PI;
			}
			}

			return 0.0f;
		}
	}

	void RasterizeGradient(const FGradient& Gradient, const FVector2f BoxSize, const FIntPoint TextureSize,
		TArray<uint8>& OutBgra)
	{
		OutBgra.SetNumUninitialized(TextureSize.X * TextureSize.Y * 4);

		const FVector2f Step(
			TextureSize.X > 1 ? BoxSize.X / static_cast<float>(TextureSize.X - 1) : 0.0f,
			TextureSize.Y > 1 ? BoxSize.Y / static_cast<float>(TextureSize.Y - 1) : 0.0f);

		for (int32 Y = 0; Y < TextureSize.Y; ++Y)
		{
			for (int32 X = 0; X < TextureSize.X; ++X)
			{
				const FVector2f Point(X * Step.X, Y * Step.Y);
				const FColor Colour = SampleStops(Gradient, EvaluateT(Gradient, Point));

				const int32 Index = (Y * TextureSize.X + X) * 4;
				OutBgra[Index + 0] = Colour.B;
				OutBgra[Index + 1] = Colour.G;
				OutBgra[Index + 2] = Colour.R;
				OutBgra[Index + 3] = Colour.A;
			}
		}
	}
}

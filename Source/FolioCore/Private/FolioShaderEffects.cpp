#include "FolioCore/FolioShaderEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace Folio
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;

		int HexDigit(const char C)
		{
			if (C >= '0' && C <= '9') return C - '0';
			if (C >= 'a' && C <= 'f') return C - 'a' + 10;
			if (C >= 'A' && C <= 'F') return C - 'A' + 10;
			return -1;
		}

		/// #rgb, #rgba, #rrggbb and #rrggbbaa, the same set RCSS accepts.
		bool ParseColour(const std::string& Token, FShaderEffect& Out)
		{
			if (Token.empty() || Token[0] != '#')
			{
				return false;
			}

			const std::string Digits = Token.substr(1);
			std::vector<int> Values;
			Values.reserve(Digits.size());
			for (const char C : Digits)
			{
				const int Digit = HexDigit(C);
				if (Digit < 0)
				{
					return false;
				}
				Values.push_back(Digit);
			}

			const auto Pair = [&Values](const std::size_t Index) { return float(Values[Index] * 16 + Values[Index + 1]) / 255.0f; };
			const auto Single = [&Values](const std::size_t Index) { return float(Values[Index] * 17) / 255.0f; };

			switch (Values.size())
			{
			case 3:
				Out.R = Single(0); Out.G = Single(1); Out.B = Single(2); Out.A = 1.0f;
				return true;
			case 4:
				Out.R = Single(0); Out.G = Single(1); Out.B = Single(2); Out.A = Single(3);
				return true;
			case 6:
				Out.R = Pair(0); Out.G = Pair(2); Out.B = Pair(4); Out.A = 1.0f;
				return true;
			case 8:
				Out.R = Pair(0); Out.G = Pair(2); Out.B = Pair(4); Out.A = Pair(6);
				return true;
			default:
				return false;
			}
		}

		unsigned char ToByte(const float Value)
		{
			const float Scaled = Value * 255.0f + 0.5f;
			if (Scaled <= 0.0f) return 0;
			if (Scaled >= 255.0f) return 255;
			return static_cast<unsigned char>(Scaled);
		}

		/// Straight colour and coverage to premultiplied RGBA.
		///
		/// An additive effect writes zero alpha, so RmlUi's `src + dst * (1 - a)` leaves what is
		/// underneath untouched and adds to it. Coverage still scales the colour, so the effect
		/// fades where it should rather than switching off.
		void Write(unsigned char* Texel, const FShaderEffect& Effect, const float Coverage)
		{
			const float Alpha = Effect.A * Coverage;
			Texel[0] = ToByte(Effect.R * Alpha);
			Texel[1] = ToByte(Effect.G * Alpha);
			Texel[2] = ToByte(Effect.B * Alpha);
			Texel[3] = Effect.bAdditive ? 0 : ToByte(Alpha);
		}
	}

	bool ParseShaderEffect(const std::string& Value, FShaderEffect& OutEffect)
	{
		std::istringstream Stream(Value);
		std::string Name;
		if (!(Stream >> Name))
		{
			return false;
		}

		OutEffect = FShaderEffect();

		if (Name == "additive")
		{
			OutEffect.Kind = FShaderEffect::EKind::Additive;
			OutEffect.bAdditive = true;
		}
		else if (Name == "scanlines")
		{
			OutEffect.Kind = FShaderEffect::EKind::Scanlines;
		}
		else if (Name == "sweep")
		{
			OutEffect.Kind = FShaderEffect::EKind::Sweep;
			OutEffect.bAdditive = true;
		}
		else
		{
			return false;
		}

		std::string Token;
		if (Stream >> Token && !ParseColour(Token, OutEffect))
		{
			return false;
		}

		const auto Number = [&Stream](float& Target)
		{
			std::string Text;
			if (Stream >> Text)
			{
				Target = float(std::atof(Text.c_str()));
			}
		};

		switch (OutEffect.Kind)
		{
		case FShaderEffect::EKind::Scanlines:
			Number(OutEffect.Period);
			Number(OutEffect.Thickness);
			break;
		case FShaderEffect::EKind::Sweep:
			Number(OutEffect.Position);
			Number(OutEffect.Width);
			Number(OutEffect.Angle);
			break;
		default:
			break;
		}

		// A period of zero would divide by zero, and a negative one means nothing.
		OutEffect.Period = std::max(OutEffect.Period, 1e-3f);
		OutEffect.Width = std::max(OutEffect.Width, 1e-3f);
		OutEffect.Thickness = std::min(std::max(OutEffect.Thickness, 0.0f), 1.0f);

		// `additive` on its own, with no colour, would be invisible rather than obviously wrong.
		return true;
	}

	void SuggestShaderEffectSize(const FShaderEffect& Effect, const int BoxWidth, const int BoxHeight,
		int& OutWidth, int& OutHeight)
	{
		const int ClampedWidth = std::max(1, BoxWidth);
		const int ClampedHeight = std::max(1, BoxHeight);

		if (Effect.Kind == FShaderEffect::EKind::Scanlines)
		{
			// Sampled rather than shaded, so the lines have to land on texels or they beat against
			// the sampling grid and the pattern crawls. Full height, one column: the pattern does
			// not vary horizontally.
			OutWidth = 1;
			OutHeight = std::min(ClampedHeight, 2048);
			return;
		}

		if (Effect.Kind == FShaderEffect::EKind::Additive)
		{
			// A flat wash needs one texel.
			OutWidth = 1;
			OutHeight = 1;
			return;
		}

		// A sweep is smooth, so a modest texture stretched over the box is enough.
		OutWidth = std::min(ClampedWidth, 256);
		OutHeight = std::min(ClampedHeight, 256);
	}

	void RasterizeShaderEffect(const FShaderEffect& Effect, const int Width, const int Height,
		std::vector<unsigned char>& OutRgba)
	{
		const int W = std::max(1, Width);
		const int H = std::max(1, Height);

		OutRgba.assign(std::size_t(W) * std::size_t(H) * 4, 0);

		const double Radians = double(Effect.Angle) * kPi / 180.0;
		const double DirX = std::cos(Radians);
		const double DirY = std::sin(Radians);

		for (int Y = 0; Y < H; ++Y)
		{
			// Texel centres, so a one-texel-tall texture samples its middle rather than its edge.
			const double V = (double(Y) + 0.5) / double(H);

			for (int X = 0; X < W; ++X)
			{
				const double U = (double(X) + 0.5) / double(W);
				float Coverage = 1.0f;

				switch (Effect.Kind)
				{
				case FShaderEffect::EKind::Scanlines:
				{
					const double Phase = std::fmod(V / double(Effect.Period), 1.0);
					Coverage = Phase < double(Effect.Thickness) ? 1.0f : 0.0f;
					break;
				}
				case FShaderEffect::EKind::Sweep:
				{
					// Distance along the sweep's direction, then a smooth band about it.
					const double Along = (U - 0.5) * DirX + (V - 0.5) * DirY + 0.5;
					const double Offset = std::fabs(Along - double(Effect.Position));
					const double Edge = double(Effect.Width) * 0.5;
					Coverage = Offset >= Edge ? 0.0f : float(1.0 - Offset / Edge);
					// Squared, so the band has a soft core rather than a linear ramp.
					Coverage *= Coverage;
					break;
				}
				default:
					break;
				}

				Write(&OutRgba[(std::size_t(Y) * std::size_t(W) + std::size_t(X)) * 4], Effect, Coverage);
			}
		}
	}
}

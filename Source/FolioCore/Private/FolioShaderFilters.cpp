#include "FolioCore/FolioShaderFilters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace Folio
{
	namespace
	{
		std::map<std::string, FShaderFilterReach>& Reaches()
		{
			static std::map<std::string, FShaderFilterReach> Map;
			return Map;
		}
	}

	void SetShaderFilterReach(const std::string& Name, FShaderFilterReach Reach)
	{
		Reaches()[Name] = std::move(Reach);
	}

	void ClearShaderFilterReach(const std::string& Name)
	{
		Reaches().erase(Name);
	}

	float ShaderFilterReach(const std::string& Name, const std::vector<FShaderFilterArg>& Args)
	{
		const auto Found = Reaches().find(Name);
		if (Found != Reaches().end() && Found->second)
		{
			return std::max(0.0f, Found->second(Args));
		}

		float Largest = 0.0f;
		for (const FShaderFilterArg& Arg : Args)
		{
			if (Arg.Kind == FShaderFilterArg::EKind::Length)
			{
				Largest = std::max(Largest, std::abs(Arg.Value));
			}
		}
		return 3.0f * Largest;
	}

	std::string EncodeShaderFilterArgs(const std::vector<FShaderFilterArg>& Args)
	{
		std::string Out;
		char Buffer[96];
		for (const FShaderFilterArg& Arg : Args)
		{
			if (!Out.empty())
			{
				Out += ' ';
			}

			switch (Arg.Kind)
			{
			case FShaderFilterArg::EKind::Number:
				std::snprintf(Buffer, sizeof(Buffer), "N%.9g", Arg.Value);
				Out += Buffer;
				break;
			case FShaderFilterArg::EKind::Length:
				std::snprintf(Buffer, sizeof(Buffer), "L%.9g", Arg.Value);
				Out += Buffer;
				break;
			case FShaderFilterArg::EKind::Colour:
				std::snprintf(Buffer, sizeof(Buffer), "C%.6g,%.6g,%.6g,%.6g",
					Arg.Colour[0], Arg.Colour[1], Arg.Colour[2], Arg.Colour[3]);
				Out += Buffer;
				break;
			case FShaderFilterArg::EKind::Word:
				Out += 'W';
				Out += Arg.Word;
				break;
			}
		}
		return Out;
	}

	bool DecodeShaderFilterArgs(const std::string& Encoded, std::vector<FShaderFilterArg>& OutArgs)
	{
		OutArgs.clear();
		std::istringstream Stream(Encoded);
		std::string Token;
		while (Stream >> Token)
		{
			FShaderFilterArg Arg;
			const std::string Body = Token.substr(1);
			switch (Token[0])
			{
			case 'N':
				Arg.Kind = FShaderFilterArg::EKind::Number;
				Arg.Value = std::strtof(Body.c_str(), nullptr);
				break;
			case 'L':
				Arg.Kind = FShaderFilterArg::EKind::Length;
				Arg.Value = std::strtof(Body.c_str(), nullptr);
				break;
			case 'C':
				Arg.Kind = FShaderFilterArg::EKind::Colour;
				if (std::sscanf(Body.c_str(), "%f,%f,%f,%f", &Arg.Colour[0], &Arg.Colour[1], &Arg.Colour[2], &Arg.Colour[3]) != 4)
				{
					return false;
				}
				break;
			case 'W':
				Arg.Kind = FShaderFilterArg::EKind::Word;
				Arg.Word = Body;
				break;
			default:
				return false;
			}
			OutArgs.push_back(std::move(Arg));
		}
		return true;
	}
}

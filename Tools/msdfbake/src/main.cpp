#include "BakeSink.h"

#include "FolioCore/FolioCoreVersion.h"
#include "FolioCore/MsdfSidecar.h"
#include "FolioCore/MsdfTypes.h"

#include "MsdfAtlas.h"
#include "MsdfGlyphGenerator.h"

#include <lodepng.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "BakeVector.h"

#include <ft2build.h>
#include FT_FREETYPE_H

using namespace Folio;

namespace
{
	void PrintUsage(std::FILE* Stream)
	{
		std::fputs(
			"msdfbake - bake distance field atlases for Folio\n"
			"\n"
			"Usage: msdfbake font   --in <font.ttf> --out <dir> [options]\n"
			"       msdfbake vector --in <icon.svg> [--in ...] --out <dir> [--rcss <file>]\n"
			"       msdfbake decode --in <atlas.png> --out <preview.png> [--scale <F>]\n"
			"       msdfbake report\n"
			"\n"
			"Common\n"
			"  --px-em <N>       Field resolution per em (default 48)\n"
			"  --px-range <N>    Field width in texels (default 12). Caps how far a font\n"
			"                    effect can reach: half of px-range/px-em, in em\n"
			"  --page <N>        Atlas page edge in texels (default 1024)\n"
			"\n"
			"font\n"
			"  --in <FILE>       Font face to bake\n"
			"  --out <DIR>       Directory to write <stem>.msdf.png and <stem>.msdf into\n"
			"  --charset <SPEC>  ascii or latin1 (default latin1)\n"
			"  --face <N>        Face index within a collection (default 0)\n"
			"\n"
			"vector\n"
			"  --in <FILE>       An SVG to bake. Repeat for more than one\n"
			"  --out <DIR>       Directory to write <name>.msdf.png and <name>.msdf into\n"
			"  --rcss <FILE>     Stylesheet to write the @spritesheet block into\n"
			"  --name <NAME>     Atlas stem (default vectors)\n"
			"  --layers          One sprite per fill colour and per stroke, with the offsets and\n"
			"                    colours to stack them back into the original drawing. Needed for\n"
			"                    any art that is not a single silhouette, which is most of it\n"
			"  --strict          Fail the run on a paint the bake cannot carry, rather than warning\n"
			"\n"
			"  Linear, radial and elliptical gradients bake. A conical or mesh fill does not: SVG has\n"
			"  neither, so Affinity exports the fill as an embedded bitmap, and the drawing bakes as an\n"
			"  outline with nothing in it. That and every other unrepresentable paint is reported by\n"
			"  name rather than left to be discovered from the render.\n"
			"\n"
			"  A distance field carries coverage and nothing else, so one field is one silhouette,\n"
			"  coloured where it is drawn. Multi-colour or stroked art therefore needs --layers,\n"
			"  which bakes each silhouette separately and writes the RCSS to stack them. Without\n"
			"  it every filled path merges into one shape and strokes vanish; both are reported.\n"
			"\n"
			"decode\n"
			"  Resolves a field the way the shader does, so a bake can be checked without a\n"
			"  renderer. Raw channels always look wrong; only the decode tells you anything.\n"
			"  --scale <F>       Output scale (default 1). Above 1 shows whether the field\n"
			"                    stays sharp when magnified, which is the point of MSDF\n"
			"\n"
			"Exit codes: 0 ok, 1 bake failed, 2 usage\n",
			Stream);
	}

	bool TakeValue(int Argc, char** Argv, int& Index, std::string& OutValue)
	{
		if (Index + 1 >= Argc)
		{
			return false;
		}
		OutValue = Argv[++Index];
		return true;
	}

	std::string StemOf(const std::string& Path)
	{
		const std::size_t Slash = Path.find_last_of("/\\");
		const std::string Name = Slash == std::string::npos ? Path : Path.substr(Slash + 1);
		const std::size_t Dot = Name.find_last_of('.');
		return Dot == std::string::npos ? Name : Name.substr(0, Dot);
	}

	/// Codepoints a bake covers. Kept coarse on purpose: the runtime generates anything missing,
	/// so a charset only decides what is free at startup rather than what can be displayed.
	std::vector<unsigned long> BuildCharset(const std::string& Spec)
	{
		std::vector<unsigned long> Codepoints;
		const unsigned long Last = Spec == "ascii" ? 0x7Eul : 0xFFul;
		for (unsigned long Code = 0x20; Code <= Last; ++Code)
		{
			Codepoints.push_back(Code);
		}
		return Codepoints;
	}

	float Median(float A, float B, float C)
	{
		return std::fmax(std::fmin(A, B), std::fmin(std::fmax(A, B), C));
	}

	/// Mirrors FolioMsdfShade's fill branch on the CPU, including the screen-space spread, with
	/// the derivative known exactly because the sample grid is uniform.
	int DecodeAtlas(int Argc, char** Argv)
	{
		std::string InputPath, OutputPath, Value;
		float Scale = 1.0f;

		for (int Index = 2; Index < Argc; ++Index)
		{
			const std::string Arg = Argv[Index];
			bool Ok = true;

			if (Arg == "--in")        Ok = TakeValue(Argc, Argv, Index, InputPath);
			else if (Arg == "--out")  Ok = TakeValue(Argc, Argv, Index, OutputPath);
			else if (Arg == "--scale"){ Ok = TakeValue(Argc, Argv, Index, Value); Scale = float(std::atof(Value.c_str())); }
			else
			{
				std::fprintf(stderr, "msdfbake: error: unknown option %s\n", Arg.c_str());
				return 2;
			}

			if (!Ok)
			{
				std::fprintf(stderr, "msdfbake: error: %s needs a value\n", Arg.c_str());
				return 2;
			}
		}

		if (InputPath.empty() || OutputPath.empty() || Scale <= 0.0f)
		{
			std::fprintf(stderr, "msdfbake: error: decode needs --in and --out, and a positive --scale\n");
			return 2;
		}

		FMsdfSidecar Sidecar;
		{
			std::ifstream SidecarFile(MsdfSidecarPathFor(InputPath), std::ios::binary);
			if (!SidecarFile)
			{
				std::fprintf(stderr, "msdfbake: error: no sidecar beside %s\n", InputPath.c_str());
				return 1;
			}
			const std::string Text((std::istreambuf_iterator<char>(SidecarFile)), std::istreambuf_iterator<char>());
			if (!ParseMsdfSidecar(Text, Sidecar))
			{
				std::fprintf(stderr, "msdfbake: error: the sidecar beside %s is unreadable\n", InputPath.c_str());
				return 1;
			}
		}

		std::vector<unsigned char> Source;
		unsigned Width = 0, Height = 0;
		if (const unsigned Error = lodepng::decode(Source, Width, Height, InputPath))
		{
			std::fprintf(stderr, "msdfbake: error: %s: %s\n", InputPath.c_str(), lodepng_error_text(Error));
			return 1;
		}

		const unsigned OutWidth = unsigned(float(Width) * Scale);
		const unsigned OutHeight = unsigned(float(Height) * Scale);
		std::vector<unsigned char> Output(std::size_t(OutWidth) * std::size_t(OutHeight) * 4, 0);

		// One output pixel covers 1/Scale texels, and the field carries PxRange texels of
		// distance across its full 0..1 span. Together those give the same spread the shader
		// derives from fwidth.
		const float Spread = std::fmax(float(Sidecar.Params.PxRange) * Scale, 1.0f);

		// Bilinear, because the GPU samples that way and nearest-neighbour would make a correct
		// field look like a broken one: every curve turns into stair steps that the hardware
		// would never produce.
		const auto Sample = [&](float U, float V, int Channel) -> float
		{
			const float Fx = std::fmax(U - 0.5f, 0.0f);
			const float Fy = std::fmax(V - 0.5f, 0.0f);
			const unsigned X0 = std::min(unsigned(Fx), Width - 1);
			const unsigned Y0 = std::min(unsigned(Fy), Height - 1);
			const unsigned X1 = std::min(X0 + 1, Width - 1);
			const unsigned Y1 = std::min(Y0 + 1, Height - 1);
			const float Tx = Fx - float(X0);
			const float Ty = Fy - float(Y0);

			const auto At = [&](unsigned Px, unsigned Py)
			{
				return float(Source[(std::size_t(Py) * std::size_t(Width) + Px) * 4 + std::size_t(Channel)]) / 255.0f;
			};

			const float Top = At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * Tx;
			const float Bottom = At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * Tx;
			return Top + (Bottom - Top) * Ty;
		};

		for (unsigned Y = 0; Y < OutHeight; ++Y)
		{
			for (unsigned X = 0; X < OutWidth; ++X)
			{
				const float U = (float(X) + 0.5f) / Scale;
				const float V = (float(Y) + 0.5f) / Scale;

				const float Distance = Median(Sample(U, V, 0), Sample(U, V, 1), Sample(U, V, 2));
				float Coverage = (Distance - 0.5f) * Spread + 0.5f;
				Coverage = std::fmin(std::fmax(Coverage, 0.0f), 1.0f);

				unsigned char* Dest = &Output[(std::size_t(Y) * std::size_t(OutWidth) + X) * 4];
				Dest[0] = Dest[1] = Dest[2] = 0;
				Dest[3] = static_cast<unsigned char>(Coverage * 255.0f + 0.5f);
			}
		}

		if (const unsigned Error = lodepng::encode(OutputPath, Output, OutWidth, OutHeight))
		{
			std::fprintf(stderr, "msdfbake: error: %s: %s\n", OutputPath.c_str(), lodepng_error_text(Error));
			return 1;
		}

		std::printf("%s %ux%u scale=%.2f pxrange=%d\n", OutputPath.c_str(), OutWidth, OutHeight,
			double(Scale), Sidecar.Params.PxRange);
		return 0;
	}

	int BakeFont(int Argc, char** Argv)
	{
		std::string InputPath, OutputDir, CharsetSpec = "latin1", Value;
		FMsdfAtlasParams Params;
		int PageSize = 1024;
		int FaceIndex = 0;

		for (int Index = 2; Index < Argc; ++Index)
		{
			const std::string Arg = Argv[Index];
			bool Ok = true;

			if (Arg == "--in")            Ok = TakeValue(Argc, Argv, Index, InputPath);
			else if (Arg == "--out")      Ok = TakeValue(Argc, Argv, Index, OutputDir);
			else if (Arg == "--charset")  Ok = TakeValue(Argc, Argv, Index, CharsetSpec);
			else if (Arg == "--px-em")   { Ok = TakeValue(Argc, Argv, Index, Value); Params.PxPerEm = std::atoi(Value.c_str()); }
			else if (Arg == "--px-range"){ Ok = TakeValue(Argc, Argv, Index, Value); Params.PxRange = std::atoi(Value.c_str()); }
			else if (Arg == "--page")    { Ok = TakeValue(Argc, Argv, Index, Value); PageSize = std::atoi(Value.c_str()); }
			else if (Arg == "--face")    { Ok = TakeValue(Argc, Argv, Index, Value); FaceIndex = std::atoi(Value.c_str()); }
			else
			{
				std::fprintf(stderr, "msdfbake: error: unknown option %s\n", Arg.c_str());
				return 2;
			}

			if (!Ok)
			{
				std::fprintf(stderr, "msdfbake: error: %s needs a value\n", Arg.c_str());
				return 2;
			}
		}

		if (InputPath.empty() || OutputDir.empty())
		{
			std::fprintf(stderr, "msdfbake: error: font needs --in and --out\n");
			return 2;
		}

		if (Params.PxPerEm <= 0 || Params.PxRange <= 0 || PageSize <= 0)
		{
			std::fprintf(stderr, "msdfbake: error: px-em, px-range and page must be positive\n");
			return 2;
		}

		FT_Library Library = nullptr;
		if (FT_Init_FreeType(&Library) != 0)
		{
			std::fprintf(stderr, "msdfbake: error: FreeType would not start\n");
			return 1;
		}

		FT_Face Face = nullptr;
		if (FT_New_Face(Library, InputPath.c_str(), FaceIndex, &Face) != 0)
		{
			std::fprintf(stderr, "msdfbake: error: cannot read %s\n", InputPath.c_str());
			FT_Done_FreeType(Library);
			return 1;
		}

		msdfbake::BakeSink Sink;
		FMsdfAtlas Atlas(Sink, Params, PageSize);

		int Packed = 0, Empty = 0, Failed = 0;
		for (const unsigned long Codepoint : BuildCharset(CharsetSpec))
		{
			const unsigned int GlyphIndex = FT_Get_Char_Index(Face, Codepoint);
			if (GlyphIndex == 0)
			{
				continue;
			}

			float AdvanceEm = 0.0f;
			GetGlyphAdvanceEm(Face, GlyphIndex, AdvanceEm);

			FMsdfGlyphBitmap Bitmap;
			if (!GenerateGlyphMsdf(Face, GlyphIndex, Params, Bitmap))
			{
				++Failed;
				continue;
			}

			FGlyphBox Box;
			if (!Atlas.Add(Bitmap, AdvanceEm, Box))
			{
				++Failed;
				continue;
			}

			Box.bEmpty ? ++Empty : ++Packed;
		}

		FT_Done_Face(Face);
		FT_Done_FreeType(Library);

		if (Packed == 0)
		{
			std::fprintf(stderr, "msdfbake: error: no glyphs were packed\n");
			return 1;
		}

		const std::string Stem = StemOf(InputPath);
		const std::string Base = OutputDir + "/" + Stem;

		for (int PageIndex = 0; PageIndex < Atlas.GetPageCount(); ++PageIndex)
		{
			const std::string Suffix = Atlas.GetPageCount() > 1 ? "_" + std::to_string(PageIndex) : "";
			const std::string PngPath = Base + Suffix + ".msdf.png";

			const std::string Error = Sink.WritePng(PageIndex, PngPath);
			if (!Error.empty())
			{
				std::fprintf(stderr, "msdfbake: error: %s: %s\n", PngPath.c_str(), Error.c_str());
				return 1;
			}

			FMsdfSidecar Sidecar;
			Sidecar.Version = MsdfSidecarVersion;
			Sidecar.Params = Params;

			std::ofstream SidecarFile(MsdfSidecarPathFor(PngPath), std::ios::binary);
			if (!SidecarFile)
			{
				std::fprintf(stderr, "msdfbake: error: cannot write the sidecar for %s\n", PngPath.c_str());
				return 1;
			}
			SidecarFile << WriteMsdfSidecar(Sidecar);

			std::printf("%s\n", PngPath.c_str());
		}

		std::printf("packed=%d empty=%d failed=%d pages=%d pxem=%d pxrange=%d maxeffect=%.3fem\n",
			Packed, Empty, Failed, Atlas.GetPageCount(), Params.PxPerEm, Params.PxRange,
			Params.MaxEffectEm());

		return 0;
	}
}

int main(int Argc, char** Argv)
{
	if (Argc < 2)
	{
		PrintUsage(stderr);
		return 2;
	}

	const std::string Mode = Argv[1];

	if (Mode == "-h" || Mode == "--help")
	{
		PrintUsage(stdout);
		return 0;
	}

	if (Mode == "report")
	{
		std::printf("%s\n", GetCoreVersionString().c_str());
		return 0;
	}

	if (Mode == "font")
	{
		return BakeFont(Argc, Argv);
	}

	if (Mode == "vector")
	{
		return msdfbake::BakeVectors(Argc, Argv);
	}

	if (Mode == "decode")
	{
		return DecodeAtlas(Argc, Argv);
	}

	std::fprintf(stderr, "msdfbake: error: unknown mode %s\n", Mode.c_str());
	PrintUsage(stderr);
	return 2;
}

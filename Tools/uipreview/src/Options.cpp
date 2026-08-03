#include "Options.h"

#include "Bake.h"

#include <cstdlib>
#include <cstring>

namespace uipreview {

namespace {

bool ParseSize(const std::string& value, int& out_width, int& out_height)
{
	const size_t x = value.find_first_of("xX");
	if (x == std::string::npos)
		return false;

	char* end = nullptr;
	const long width = std::strtol(value.c_str(), &end, 10);
	if (end != value.c_str() + x)
		return false;
	const long height = std::strtol(value.c_str() + x + 1, &end, 10);
	if (*end != '\0')
		return false;

	if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
		return false;

	out_width = (int)width;
	out_height = (int)height;
	return true;
}

/** Reads the value belonging to a flag, reporting a clear error when it is missing. */
bool TakeValue(int argc, char** argv, int& i, const char* flag, std::string& out, std::string& error)
{
	if (i + 1 >= argc)
	{
		error = std::string(flag) + " expects a value";
		return false;
	}
	out = argv[++i];
	return true;
}

} // namespace

void PrintUsage(std::FILE* stream)
{
	std::fputs(
		"uipreview - render and verify RmlUi documents\n"
		"\n"
		"Usage: uipreview <document.rml> [options]\n"
		"\n"
		"Display\n"
		"  --size <WxH>          Viewport in framebuffer pixels (default 1920x1080)\n"
		"  --dp-ratio <F>        Pin the density independent pixel ratio. Without it the ratio\n"
		"                        follows the viewport, as Unreal scales a resized game window\n"
		"  --dp-reference <N>    Shortest side that means a ratio of 1 (default 1080)\n"
		"  --root <DIR>          Directory paths resolve against (default: derived)\n"
		"  --font <FILE>         Register a font face. Repeatable. No typeface is bundled;\n"
		"                        when omitted, fonts under <root>/UI/fonts are used, then\n"
		"                        the engine's Roboto\n"
		"  --engine <DIR>        Unreal engine directory, for the default Roboto. Also read\n"
		"                        from UE_ENGINE_DIR; otherwise located automatically\n"
		"  --debug               Open the RmlUi debugger. F8 toggles it while running\n"
		"\n"
		"Capture\n"
		"  --screenshot <FILE>   Render to a PNG and exit\n"
		"  --shot-each <DIR>     With --keys, write a PNG after each input\n"
		"  --filmstrip <DIR>     Write one PNG per frame from the document's first, with a\n"
		"                        filmstrip.json naming them. Captures an animation over time\n"
		"                        rather than the state it settles into\n"
		"  --filmstrip-frames <N>  How many frames the filmstrip is (default 60)\n"
		"  --frames <N>          Frames pumped before a capture (default 30)\n"
		"  --dt <MS>             Simulated milliseconds per frame (default 16.67, 0 freezes)\n"
		"  --bake <SEL>=<FILE>   Render one element alone, on nothing, cropped to its box, and\n"
		"                        write it as a PNG with straight alpha. Repeatable. Turns an\n"
		"                        effect that costs a render target every frame into a texture\n"
		"  --bake-margin <PX>    Room around a baked element, for an effect reaching outside it\n"
		"\n"
		"Content\n"
		"  --data <FILE>         Mount a JSON file as the document's data model\n"
		"  --data-model <NAME>   Override the data model name\n"
		"  --culture <CODE>      Load UI/loc/<CODE>.json, or 'pseudo' for pseudo-localization\n"
		"  --input-device <D>    What the document is told the player holds: keyboard, mouse or\n"
		"                        gamepad (default keyboard). Sets the folio-device-* body classes\n"
		"                        and the reserved 'folio' data model\n"
		"  --confirm-button <B>  Which gamepad face button confirms: south or east (default south)\n"
		"\n"
		"Navigation\n"
		"  --keys <LIST>         Inject a comma separated input sequence, then report focus.\n"
		"                        up down left right confirm back tab shift+tab space\n"
		"                        pageup pagedown home end a-z 0-9 wait[:N]\n"
		"                        click:<selector> text:<string>\n"
		"                        Letters win over gamepad face button names: use confirm/back\n"
		"  --dump-tree           Print the element tree with focus and nav attributes\n"
		"\n"
		"Reporting\n"
		"  --strict              Warnings fail the run, and enable the markup lint checks\n"
		"  --json-report <FILE>  Write a machine readable report\n"
		"  --a11y                Check contrast, text size, hit target size, control names and\n"
		"                        whether focus is visible. Findings are warnings, so --strict\n"
		"                        fails the run on them\n"
		"  --a11y-report <FILE>  The same findings as JSON. Implies --a11y\n"
		"  --a11y-tree           Print the tree a screen reader is read out of, in reading order\n"
		"  --a11y-tree-report <FILE>  The same tree as JSON\n"
		"  --a11y-contrast <N>   The ratio normal text is held to, 4.5 by default. 7 is what a\n"
		"                        high-contrast theme should meet. Implies --a11y\n"
		"  --text-scale <N>      Scale the type the way the accessibility setting does, 0.5 to 3.\n"
		"                        Above 1, --a11y also reports text the setting cannot move and\n"
		"                        text that no longer fits\n"
		"  --reduced-motion      Turn every transition and animation off, as the setting does\n"
		"  --text-speed <N>      How fast <text> types, 1 being as authored, 0.1 to 10\n"
		"  --instant-text        Every <text> arrives whole, as the setting does\n"
		"  --no-screen-effects   Hide the layers marked folio-screen-effect: the vignettes, tints\n"
		"                        and flashes a game paints over the whole view\n"
		"  --flash-check         Render --filmstrip-frames frames and report flashing over the\n"
		"                        WCAG limit of three a second, which is a seizure risk and a\n"
		"                        certification question\n"
		"  --simulate <VISION>   Redraw captures, and re-check contrast, as protanopia,\n"
		"                        deuteranopia, tritanopia or achromatopsia sees them\n"
		"  --metrics-report <FILE>  Write where every piece of text laid out, for diffing a\n"
		"                        change to the text engine against a known good run\n"
		"  --reload-textures     Release every texture between two renders before capturing,\n"
		"                        which is what a hot reload does. The image must not change\n"
		"  --no-assets-wanted    Do not update ASSETS_WANTED.md\n"
		"  --no-watch            Do not watch for file changes\n"
		"  -v, --verbose         Show informational messages\n"
		"  --font-report         Report the text engine. Without a document, prints what this\n"
		"                        binary was built against and exits\n"
		"  --simple-shaping      Measure one glyph per codepoint with legacy kerning instead of\n"
		"                        shaping with HarfBuzz\n"
		"  -h, --help            This text\n"
		"\n"
		"Exit codes: 0 ok, 1 document errors, 2 usage, 3 init failure, 4 runtime failure,\n"
		"            5 warnings present under --strict\n",
		stream);
}

ParseResult ParseCommandLine(int argc, char** argv, Options& o, std::string& error)
{
	std::string value;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];

		if (arg == "-h" || arg == "--help")
			return ParseResult::HelpRequested;

		if (arg == "--font-report")
		{
			o.font_report = true;
			continue;
		}

		if (arg == "-v" || arg == "--verbose")
			o.verbose = true;
		else if (arg == "--debug")
			o.debug = true;
		else if (arg == "--strict")
			o.strict = true;
		else if (arg == "--no-watch")
			o.watch = false;
		else if (arg == "--no-font-cache")
			o.font_cache = false;
		else if (arg == "--simple-shaping")
			o.complex_shaping = false;
		else if (arg == "--no-assets-wanted")
			o.write_assets_wanted = false;
		else if (arg == "--reload-textures")
			o.reload_textures = true;
		else if (arg == "--dump-tree")
			o.dump_tree = true;
		else if (arg == "--size")
		{
			if (!TakeValue(argc, argv, i, "--size", value, error))
				return ParseResult::Error;
			if (!ParseSize(value, o.width, o.height))
			{
				error = "--size expects WxH with both between 1 and 16384, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--dp-ratio")
		{
			if (!TakeValue(argc, argv, i, "--dp-ratio", value, error))
				return ParseResult::Error;
			o.dp_ratio = (float)std::atof(value.c_str());
			if (o.dp_ratio <= 0.0f)
			{
				error = "--dp-ratio expects a positive number, got '" + value + "'";
				return ParseResult::Error;
			}
			o.dp_ratio_fixed = true;
		}
		else if (arg == "--dp-reference")
		{
			if (!TakeValue(argc, argv, i, "--dp-reference", value, error))
				return ParseResult::Error;
			o.dp_reference = std::atoi(value.c_str());
			if (o.dp_reference <= 0)
			{
				error = "--dp-reference expects a positive number, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--msdf-px-em")
		{
			if (!TakeValue(argc, argv, i, "--msdf-px-em", value, error))
				return ParseResult::Error;
			o.msdf_px_em = std::atoi(value.c_str());
		}
		else if (arg == "--msdf-px-range")
		{
			if (!TakeValue(argc, argv, i, "--msdf-px-range", value, error))
				return ParseResult::Error;
			o.msdf_px_range = std::atoi(value.c_str());
		}
		else if (arg == "--msdf-sharpen")
		{
			if (!TakeValue(argc, argv, i, "--msdf-sharpen", value, error))
				return ParseResult::Error;
			o.msdf_sharpen = (float)std::atof(value.c_str());
		}
		else if (arg == "--frames")
		{
			if (!TakeValue(argc, argv, i, "--frames", value, error))
				return ParseResult::Error;
			o.frames = std::atoi(value.c_str());
			if (o.frames < 1)
			{
				error = "--frames expects a positive integer, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--dt")
		{
			if (!TakeValue(argc, argv, i, "--dt", value, error))
				return ParseResult::Error;
			o.dt_ms = std::atof(value.c_str());
			if (o.dt_ms < 0.0)
			{
				error = "--dt expects a non-negative number, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--root")
		{
			if (!TakeValue(argc, argv, i, "--root", o.root, error))
				return ParseResult::Error;
		}
		else if (arg == "--engine")
		{
			if (!TakeValue(argc, argv, i, "--engine", o.engine, error))
				return ParseResult::Error;
		}
		else if (arg == "--font")
		{
			if (!TakeValue(argc, argv, i, "--font", value, error))
				return ParseResult::Error;
			o.fonts.push_back(value);
		}
		else if (arg == "--data")
		{
			if (!TakeValue(argc, argv, i, "--data", o.data_path, error))
				return ParseResult::Error;
		}
		else if (arg == "--data-model")
		{
			if (!TakeValue(argc, argv, i, "--data-model", o.data_model, error))
				return ParseResult::Error;
		}
		else if (arg == "--theme")
		{
			std::string theme;
			if (!TakeValue(argc, argv, i, "--theme", theme, error))
				return ParseResult::Error;
			o.themes.push_back(theme);
		}
		else if (arg == "--a11y-contrast")
		{
			if (!TakeValue(argc, argv, i, "--a11y-contrast", value, error))
				return ParseResult::Error;
			o.a11y_contrast = (float)std::atof(value.c_str());
			if (o.a11y_contrast < 1.0f || o.a11y_contrast > 21.0f)
			{
				error = "--a11y-contrast expects a ratio between 1 and 21, got '" + value + "'";
				return ParseResult::Error;
			}
			o.a11y = true;
		}
		else if (arg == "--text-scale")
		{
			if (!TakeValue(argc, argv, i, "--text-scale", value, error))
				return ParseResult::Error;
			o.text_scale = (float)std::atof(value.c_str());
			if (o.text_scale < 0.5f || o.text_scale > 3.0f)
			{
				error = "--text-scale expects a number between 0.5 and 3, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--reduced-motion")
		{
			o.reduced_motion = true;
		}
		else if (arg == "--flash-check")
		{
			o.flash_check = true;
		}
		else if (arg == "--no-screen-effects")
		{
			o.no_screen_effects = true;
		}
		else if (arg == "--instant-text")
		{
			o.instant_text = true;
		}
		else if (arg == "--text-speed")
		{
			if (!TakeValue(argc, argv, i, "--text-speed", value, error))
				return ParseResult::Error;
			o.text_speed = (float)std::atof(value.c_str());
			if (o.text_speed < 0.1f || o.text_speed > 10.0f)
			{
				error = "--text-speed expects a number between 0.1 and 10, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--simulate")
		{
			if (!TakeValue(argc, argv, i, "--simulate", value, error))
				return ParseResult::Error;
			if (!ParseColourVision(value, o.simulate))
			{
				error = "--simulate expects protanopia, deuteranopia, tritanopia, achromatopsia or "
					"none, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--culture")
		{
			if (!TakeValue(argc, argv, i, "--culture", o.culture, error))
				return ParseResult::Error;
		}
		else if (arg == "--input-device")
		{
			std::string device;
			if (!TakeValue(argc, argv, i, "--input-device", device, error))
				return ParseResult::Error;

			if (device == "keyboard")      o.input_device = Folio::EInputDevice::Keyboard;
			else if (device == "mouse")    o.input_device = Folio::EInputDevice::Mouse;
			else if (device == "gamepad")  o.input_device = Folio::EInputDevice::Gamepad;
			else
			{
				error = "--input-device expects keyboard, mouse or gamepad";
				return ParseResult::Error;
			}
		}
		else if (arg == "--confirm-button")
		{
			std::string button;
			if (!TakeValue(argc, argv, i, "--confirm-button", button, error))
				return ParseResult::Error;

			if (button == "south")     o.confirm_button = Folio::EConfirmButton::South;
			else if (button == "east") o.confirm_button = Folio::EConfirmButton::East;
			else
			{
				error = "--confirm-button expects south or east";
				return ParseResult::Error;
			}
		}
		else if (arg == "--keys")
		{
			if (!TakeValue(argc, argv, i, "--keys", o.keys, error))
				return ParseResult::Error;
		}
		else if (arg == "--shot-each")
		{
			if (!TakeValue(argc, argv, i, "--shot-each", o.shot_each, error))
				return ParseResult::Error;
		}
		else if (arg == "--filmstrip")
		{
			if (!TakeValue(argc, argv, i, "--filmstrip", o.filmstrip, error))
				return ParseResult::Error;
		}
		else if (arg == "--filmstrip-frames")
		{
			if (!TakeValue(argc, argv, i, "--filmstrip-frames", value, error))
				return ParseResult::Error;
			o.filmstrip_frames = std::atoi(value.c_str());
			if (o.filmstrip_frames < 1 || o.filmstrip_frames > 3600)
			{
				error = "--filmstrip-frames expects 1 to 3600, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--bake")
		{
			if (!TakeValue(argc, argv, i, "--bake", value, error))
				return ParseResult::Error;

			std::string selector;
			std::string path;
			if (!ParseBakeArgument(value, selector, path))
			{
				error = "--bake expects <selector>=<out.png>, got '" + value + "'";
				return ParseResult::Error;
			}
			o.bakes.push_back(value);
		}
		else if (arg == "--bake-margin")
		{
			if (!TakeValue(argc, argv, i, "--bake-margin", value, error))
				return ParseResult::Error;
			o.bake_margin = std::atoi(value.c_str());
			if (o.bake_margin < 0 || o.bake_margin > 4096)
			{
				error = "--bake-margin expects 0 to 4096, got '" + value + "'";
				return ParseResult::Error;
			}
		}
		else if (arg == "--screenshot")
		{
			if (!TakeValue(argc, argv, i, "--screenshot", o.screenshot, error))
				return ParseResult::Error;
		}
		else if (arg == "--json-report")
		{
			if (!TakeValue(argc, argv, i, "--json-report", o.json_report, error))
				return ParseResult::Error;
		}
		else if (arg == "--provenance-report")
		{
			if (!TakeValue(argc, argv, i, "--provenance-report", o.provenance_report, error))
				return ParseResult::Error;
		}
		else if (arg == "--a11y")
		{
			o.a11y = true;
		}
		else if (arg == "--a11y-report")
		{
			if (!TakeValue(argc, argv, i, "--a11y-report", o.a11y_report, error))
				return ParseResult::Error;
		}
		else if (arg == "--a11y-tree")
		{
			o.a11y_tree = true;
		}
		else if (arg == "--a11y-tree-report")
		{
			if (!TakeValue(argc, argv, i, "--a11y-tree-report", o.a11y_tree_report, error))
				return ParseResult::Error;
		}
		else if (arg == "--metrics-report")
		{
			if (!TakeValue(argc, argv, i, "--metrics-report", o.metrics_report, error))
				return ParseResult::Error;
		}
		else if (!arg.empty() && arg[0] == '-')
		{
			error = "unknown option '" + arg + "'";
			return ParseResult::Error;
		}
		else if (o.document.empty())
		{
			o.document = arg;
		}
		else
		{
			error = "unexpected argument '" + arg + "'";
			return ParseResult::Error;
		}
	}

	// Without a document there is nothing to render, but the text engine can still say what it
	// was built against, which answers "is this binary using the field pipeline at all".
	if (o.document.empty())
	{
		if (o.font_report)
			return ParseResult::FontReportRequested;

		error = "no document given";
		return ParseResult::Error;
	}

	if (!o.shot_each.empty() && o.keys.empty())
	{
		error = "--shot-each requires --keys";
		return ParseResult::Error;
	}

	if (!o.filmstrip.empty() && o.dt_ms <= 0.0)
	{
		error = "--filmstrip needs --dt above zero, or every frame is the same instant";
		return ParseResult::Error;
	}

	// Watching a document the process is about to stop rendering serves no purpose.
	if (o.IsOneShot())
		o.watch = false;

	return ParseResult::Ok;
}

} // namespace uipreview

#include "InputScript.h"

#include "App.h"
#include "Diagnostics.h"
#include "ElementPath.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace uipreview {

namespace {

struct KeyName {
	const char* token;
	Rml::Input::KeyIdentifier key;
};

// Gamepad face button names would collide with the letter keys, so the semantic names
// confirm/back are the ones that exist. Letters stay letters.
const KeyName kKeyNames[] = {
	{"up", Rml::Input::KI_UP},
	{"down", Rml::Input::KI_DOWN},
	{"left", Rml::Input::KI_LEFT},
	{"right", Rml::Input::KI_RIGHT},
	{"confirm", Rml::Input::KI_RETURN},
	{"enter", Rml::Input::KI_RETURN},
	{"return", Rml::Input::KI_RETURN},
	{"back", Rml::Input::KI_ESCAPE},
	{"cancel", Rml::Input::KI_ESCAPE},
	{"esc", Rml::Input::KI_ESCAPE},
	{"escape", Rml::Input::KI_ESCAPE},
	{"tab", Rml::Input::KI_TAB},
	{"space", Rml::Input::KI_SPACE},
	{"pageup", Rml::Input::KI_PRIOR},
	{"lb", Rml::Input::KI_PRIOR},
	{"pagedown", Rml::Input::KI_NEXT},
	{"rb", Rml::Input::KI_NEXT},
	{"home", Rml::Input::KI_HOME},
	{"end", Rml::Input::KI_END},
};

std::string Lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return text;
}

std::string Trim(const std::string& text)
{
	const size_t begin = text.find_first_not_of(" \t");
	const size_t end = text.find_last_not_of(" \t");
	return begin == std::string::npos ? std::string() : text.substr(begin, end - begin + 1);
}

std::string Sanitize(const std::string& token)
{
	std::string out;
	for (const char c : Lower(token))
		out += std::isalnum((unsigned char)c) ? c : '_';
	return out;
}

} // namespace

bool ParseInputScript(const std::string& text, std::vector<InputStep>& out_steps, std::string& out_error)
{
	std::istringstream stream(text);
	std::string raw;

	while (std::getline(stream, raw, ','))
	{
		const std::string token = Trim(raw);
		if (token.empty())
			continue;

		InputStep step;
		step.token = token;

		const std::string lowered = Lower(token);

		if (lowered.rfind("click:", 0) == 0)
		{
			step.kind = InputStep::Kind::Click;
			step.argument = token.substr(6);
			out_steps.push_back(step);
			continue;
		}
		if (lowered.rfind("text:", 0) == 0)
		{
			step.kind = InputStep::Kind::Text;
			step.argument = token.substr(5);
			out_steps.push_back(step);
			continue;
		}
		if (lowered == "wait" || lowered.rfind("wait:", 0) == 0)
		{
			step.kind = InputStep::Kind::Wait;
			step.frames = (lowered.size() > 5) ? std::atoi(lowered.c_str() + 5) : 0;
			out_steps.push_back(step);
			continue;
		}

		std::string key_token = lowered;
		if (key_token.rfind("shift+", 0) == 0)
		{
			step.modifiers |= Rml::Input::KM_SHIFT;
			key_token = key_token.substr(6);
		}
		if (key_token.rfind("ctrl+", 0) == 0)
		{
			step.modifiers |= Rml::Input::KM_CTRL;
			key_token = key_token.substr(5);
		}

		bool matched = false;
		for (const KeyName& name : kKeyNames)
		{
			if (key_token == name.token)
			{
				step.key = name.key;
				matched = true;
				break;
			}
		}

		if (!matched && key_token.size() == 1)
		{
			const char c = key_token[0];
			if (c >= 'a' && c <= 'z')
			{
				step.key = (Rml::Input::KeyIdentifier)(Rml::Input::KI_A + (c - 'a'));
				matched = true;
			}
			else if (c >= '0' && c <= '9')
			{
				step.key = (Rml::Input::KeyIdentifier)(Rml::Input::KI_0 + (c - '0'));
				matched = true;
			}
		}

		if (!matched)
		{
			out_error = "unknown key token '" + token + "'";
			return false;
		}

		out_steps.push_back(step);
	}

	if (out_steps.empty())
	{
		out_error = "no input steps given";
		return false;
	}
	return true;
}

std::vector<FocusReport> RunInputScript(App& app, const std::vector<InputStep>& steps, const std::string& shot_directory,
	Diagnostics& diagnostics)
{
	std::vector<FocusReport> reports;

	Rml::Context* context = app.GetContext();
	Rml::ElementDocument* document = app.GetDocument();

	// A freshly shown document has nothing focused: RmlUi does not do it automatically, so
	// without this the first directional key has no origin to move from.
	if (!context->GetFocusElement() || context->GetFocusElement() == document)
	{
		if (Rml::Element* autofocus = document->QuerySelector("[autofocus]"))
			autofocus->Focus(true);
		else
			document->Focus();
	}

	app.PumpFrames(1);

	auto capture = [&](int index, const std::string& token) -> std::string {
		if (shot_directory.empty())
			return {};
		char name[64];
		std::snprintf(name, sizeof name, "%03d_%s.png", index, Sanitize(token).c_str());
		const std::string path = shot_directory + "/" + name;
		return app.CaptureTo(path) ? path : std::string();
	};

	{
		FocusReport initial;
		initial.step = 0;
		initial.token = "initial";
		Rml::Element* focused = context->GetFocusElement();
		initial.focus = ComputeElementPath(focused);
		initial.description = DescribeElement(focused);
		initial.screenshot = capture(0, "initial");
		reports.push_back(initial);

		if (!focused || focused == document)
		{
			diagnostics.Report(Severity::Warning, "W-NAV-NOFOCUS",
				"nothing is focused before the first input; mark a starting element with autofocus");
		}
	}

	int index = 1;
	for (const InputStep& step : steps)
	{
		Rml::Element* before = context->GetFocusElement();

		switch (step.kind)
		{
		case InputStep::Kind::Key:
			context->ProcessKeyDown(step.key, step.modifiers);
			context->Update();
			context->ProcessKeyUp(step.key, step.modifiers);
			break;

		case InputStep::Kind::Text:
			context->ProcessTextInput(Rml::String(step.argument));
			break;

		case InputStep::Kind::Click:
			if (Rml::Element* target = document->QuerySelector(step.argument))
			{
				const Rml::Vector2f offset = target->GetAbsoluteOffset(Rml::BoxArea::Border);
				const Rml::Vector2f size = target->GetBox().GetSize(Rml::BoxArea::Border);
				const Rml::Vector2i centre((int)(offset.x + size.x * 0.5f), (int)(offset.y + size.y * 0.5f));
				context->ProcessMouseMove(centre.x, centre.y, 0);
				context->ProcessMouseButtonDown(0, 0);
				context->Update();
				context->ProcessMouseButtonUp(0, 0);
			}
			else
			{
				diagnostics.Report(Severity::Warning, "W-NAV-NOTARGET", "no element matches '" + step.argument + "'");
			}
			break;

		case InputStep::Kind::Wait: break;
		}

		app.PumpFrames(step.kind == InputStep::Kind::Wait && step.frames > 0 ? step.frames : app.FramesPerStep());

		Rml::Element* after = context->GetFocusElement();

		FocusReport report;
		report.step = index;
		report.token = step.token;
		report.focus = ComputeElementPath(after);
		report.description = DescribeElement(after);
		report.screenshot = capture(index, step.token);
		reports.push_back(report);

		// A directional key that leaves focus where it was is exactly the failure a static
		// screenshot cannot show, so it is worth naming rather than leaving to the eye.
		const bool directional = step.kind == InputStep::Kind::Key &&
			(step.key == Rml::Input::KI_UP || step.key == Rml::Input::KI_DOWN || step.key == Rml::Input::KI_LEFT ||
				step.key == Rml::Input::KI_RIGHT || step.key == Rml::Input::KI_TAB);
		if (directional && before == after)
		{
			diagnostics.Report(Severity::Warning, "W-NAV-DEADEND",
				"'" + step.token + "' did not move focus from " + ComputeElementPath(after));
		}

		++index;
	}

	if (!shot_directory.empty())
	{
		std::ofstream sequence(shot_directory + "/sequence.txt", std::ios::binary | std::ios::trunc);
		for (const FocusReport& report : reports)
		{
			sequence << "step=" << report.step << " key=" << report.token << " focus=" << report.description;
			if (!report.screenshot.empty())
				sequence << " shot=" << report.screenshot;
			sequence << "\n";
		}
	}

	return reports;
}

} // namespace uipreview

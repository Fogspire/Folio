#include "Diagnostics.h"

#include <algorithm>
#include <cstdio>
#include <regex>

namespace uipreview {

namespace {

const char* SeverityName(Severity severity)
{
	switch (severity)
	{
	case Severity::Error: return "error";
	case Severity::Warning: return "warning";
	default: return "info";
	}
}

/**
 * Maps an RmlUi message to one of our codes. The literals are those of RmlUi 6.2 and will
 * drift on a version bump, which the golden-file diagnostics test is there to catch. Anything
 * unmatched still reports, under the generic code.
 */
struct Matcher {
	const char* needle;
	const char* code;
};

// Ordered: the first match wins, so put the specific before the general.
const Matcher kMatchers[] = {
	// DecoratorNinePatch.cpp. Sprite-only decorator, never reaches LoadTexture.
	{"in ninepatch decorator", "W-ASSET-MISSING"},
	// TextureDatabase.cpp. Only reachable if a LoadTexture override returns 0.
	{"Could not load texture", "W-ASSET-MISSING"},
	// StyleSheetParser.cpp
	{"could not be instanced", "W-RCSS-DECORATOR"},
	{"Invalid decorator type", "W-RCSS-DECORATOR"},
	{"already declared, ignoring decorator", "W-RCSS-DECORATOR"},
	{"has no sprites defined", "W-RCSS-SPRITESHEET"},
	{"No image source (property 'src')", "W-RCSS-SPRITESHEET"},
	{"Spritesheet resolution", "W-RCSS-SPRITESHEET"},
	// PropertySpecification.cpp / StyleSheetParser.cpp
	{"Unknown property", "W-RCSS-PROP"},
	{"Invalid property", "W-RCSS-PROP"},
	{"Property '", "W-RCSS-PROP"},
	// FontProvider.cpp
	{"Could not load font face", "W-FONT"},
	{"Could not find font", "W-FONT"},
	// DataModel / DataExpression
	{"data variable", "W-DATA-BINDING"},
	{"data expression", "W-DATA-BINDING"},
	{"Failed to execute", "W-DATA-BINDING"},
};

std::string ClassifyRmlUi(Rml::Log::Type type, const std::string& message)
{
	for (const Matcher& matcher : kMatchers)
	{
		if (message.find(matcher.needle) != std::string::npos)
			return matcher.code;
	}
	return (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) ? "E-RMLUI" : "W-RMLUI";
}

} // namespace

std::string Diagnostics::Relative(std::string path) const
{
	std::replace(path.begin(), path.end(), '\\', '/');

	if (!root.empty())
	{
		std::string normalised_root = root;
		std::replace(normalised_root.begin(), normalised_root.end(), '\\', '/');
		if (!normalised_root.empty() && normalised_root.back() != '/')
			normalised_root += '/';

		// Compare case insensitively: Windows paths reach us in mixed case.
		if (path.size() >= normalised_root.size())
		{
			const bool matches = std::equal(normalised_root.begin(), normalised_root.end(), path.begin(),
				[](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
			if (matches)
				path.erase(0, normalised_root.size());
		}
	}

	while (!path.empty() && path.front() == '/')
		path.erase(0, 1);

	return path;
}

void Diagnostics::Report(Severity severity, std::string code, std::string message, std::string file, int line,
	nlohmann::ordered_json data)
{
	Record record;
	record.severity = severity;
	record.code = std::move(code);
	record.message = std::move(message);
	record.file = Relative(file.empty() ? current_document : std::move(file));
	record.line = line;
	record.data = std::move(data);

	if (severity == Severity::Error)
		++error_count;
	else if (severity == Severity::Warning)
		++warning_count;

	Emit(record);
	records.push_back(std::move(record));
}

void Diagnostics::FromRmlUi(Rml::Log::Type type, const std::string& message)
{
	if (type == Rml::Log::LT_INFO || type == Rml::Log::LT_DEBUG || type == Rml::Log::LT_ALWAYS)
	{
		if (verbose)
			Report(Severity::Info, "I-RMLUI", message);
		return;
	}

	const Severity severity = (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) ? Severity::Error : Severity::Warning;

	std::string text = message;
	std::string file;
	int line = 0;

	// Log::ParseError prefixes "<file>:<line>: ", so try that first.
	static const std::regex prefix_pattern(R"(^(.+?):(\d+): (.*)$)", std::regex::optimize);
	// Most warnings instead append "at <file>:<line>" or "declared at <file>:<line>".
	static const std::regex suffix_pattern(R"(\bat ([^\s:]+(?::[^\s:]*)?[^\s:]*):(\d+)\.?$)", std::regex::optimize);

	std::smatch match;
	if (std::regex_match(text, match, prefix_pattern))
	{
		file = match[1].str();
		line = std::atoi(match[2].str().c_str());
		text = match[3].str();
	}
	else if (std::regex_search(text, match, suffix_pattern))
	{
		file = match[1].str();
		line = std::atoi(match[2].str().c_str());
	}

	Report(severity, ClassifyRmlUi(type, message), text, file, line);
}

void Diagnostics::Emit(const Record& record)
{
	const std::string& file = record.file.empty() ? current_document : record.file;
	std::fprintf(stderr, "%s:%d:0: %s: [%s] %s\n", file.empty() ? "uipreview" : file.c_str(), record.line,
		SeverityName(record.severity), record.code.c_str(), record.message.c_str());
}

void Diagnostics::Reset()
{
	error_count = 0;
	warning_count = 0;
	records.clear();
}

void Diagnostics::PrintSummary(const std::string& document) const
{
	if (error_count == 0 && warning_count == 0)
		return;

	std::fprintf(stderr, "uipreview: %d error%s, %d warning%s in '%s'\n", error_count, error_count == 1 ? "" : "s",
		warning_count, warning_count == 1 ? "" : "s", document.c_str());
}

int Diagnostics::ComputeExitCode(bool strict) const
{
	if (error_count > 0)
		return 1;
	if (strict && warning_count > 0)
		return 5;
	return 0;
}

} // namespace uipreview

#include "JsonReport.h"

#include "App.h"
#include "Diagnostics.h"
#include "MissingAssets.h"
#include "Options.h"

#include <RmlUi/Core/Core.h>

#include <nlohmann/json.hpp>

#include <fstream>

using ordered_json = nlohmann::ordered_json;

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

} // namespace

bool WriteJsonReport(const std::string& path, const Options& options, const App& app, const Diagnostics& diagnostics,
	int exit_code, std::string& out_error)
{
	ordered_json report;
	report["schema"] = 1;

	report["tool"] = {{"name", "uipreview"}, {"rmlui", Rml::GetVersion()}};

	report["command"] = {
		{"document", diagnostics.Relative(options.document)},
		{"root", app.Root()},
		{"size", ordered_json::array({options.width, options.height})},
		{"dp_ratio", options.dp_ratio},
		{"culture", options.culture},
		{"strict", options.strict},
	};

	report["result"] = {
		{"exit_code", exit_code},
		{"errors", diagnostics.ErrorCount()},
		{"warnings", diagnostics.WarningCount()},
	};

	ordered_json diagnostics_array = ordered_json::array();
	for (const Record& record : diagnostics.Records())
	{
		ordered_json entry = {
			{"severity", SeverityName(record.severity)},
			{"code", record.code},
			{"file", record.file},
			{"line", record.line},
			{"message", record.message},
		};
		if (!record.data.empty())
			entry["data"] = record.data;
		diagnostics_array.push_back(std::move(entry));
	}
	report["diagnostics"] = std::move(diagnostics_array);

	ordered_json missing = ordered_json::array();
	for (const auto& [name, entry] : app.Missing().Sprites())
	{
		missing.push_back({
			{"name", entry.name},
			{"size", ordered_json::array({entry.width, entry.height})},
			{"documents", ordered_json(entry.documents)},
		});
	}
	report["missing_assets"] = std::move(missing);

	ordered_json focus = ordered_json::array();
	for (const FocusReport& step : app.FocusReports())
	{
		focus.push_back({
			{"step", step.step},
			{"key", step.token},
			{"focus", step.focus},
			{"screenshot", step.screenshot},
		});
	}
	report["focus_sequence"] = std::move(focus);

	report["screenshots"] = ordered_json(app.Screenshots());

	// Everything RmlUi actually opened, which is what a build system needs to know when to
	// re-run this document.
	ordered_json documents = ordered_json::array();
	for (const std::string& opened : app.OpenedFiles())
		documents.push_back(diagnostics.Relative(opened));
	report["documents"] = std::move(documents);

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		out_error = "could not open for writing";
		return false;
	}
	file << report.dump(2) << "\n";
	return true;
}

} // namespace uipreview

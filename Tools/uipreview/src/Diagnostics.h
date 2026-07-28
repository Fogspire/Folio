#pragma once

#include <RmlUi/Core/Log.h>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace uipreview {

enum class Severity { Info, Warning, Error };

struct Record {
	Severity severity = Severity::Info;
	/** Stable machine readable identifier, e.g. W-ASSET-MISSING. */
	std::string code;
	/** Root relative, forward slashed. Empty when there is no document context. */
	std::string file;
	int line = 0;
	std::string message;
	/** Structured extras for the JSON report, so consumers never re-parse prose. */
	nlohmann::ordered_json data;
};

/**
 * The single choke point for everything the tool reports. Emits to stderr as
 * `path:line:col: severity: [CODE] message`, and accumulates records for the JSON report.
 */
class Diagnostics {
public:
	void SetRoot(std::string in_root) { root = std::move(in_root); }
	/** Used as the file for messages RmlUi reports without any location. */
	void SetCurrentDocument(std::string document) { current_document = std::move(document); }
	void SetVerbose(bool value) { verbose = value; }

	void Report(Severity severity, std::string code, std::string message, std::string file = {}, int line = 0,
		nlohmann::ordered_json data = nlohmann::ordered_json::object());

	/** Classifies a message coming from RmlUi's logger and reports it. */
	void FromRmlUi(Rml::Log::Type type, const std::string& message);

	int ErrorCount() const { return error_count; }
	int WarningCount() const { return warning_count; }
	const std::vector<Record>& Records() const { return records; }

	/** Clears counters and records, for a hot reload. */
	void Reset();

	void PrintSummary(const std::string& document) const;
	int ComputeExitCode(bool strict) const;

	/** Converts an absolute or messy path into the root relative, forward slashed form. */
	std::string Relative(std::string path) const;

private:
	void Emit(const Record& record);

	std::string root;
	std::string current_document;
	bool verbose = false;

	int error_count = 0;
	int warning_count = 0;
	std::vector<Record> records;
};

} // namespace uipreview

#include "AccessibilityReport.h"
#include "App.h"
#include "Bake.h"
#include "Diagnostics.h"
#include "JsonReport.h"
#include "MetricsReport.h"
#include "ProvenanceReport.h"
#include "Options.h"
#include "PerformanceReport.h"

#include "FolioCore/FolioCoreVersion.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
	using namespace uipreview;

	Options options;
	std::string error;

	switch (ParseCommandLine(argc, argv, options, error))
	{
	case ParseResult::HelpRequested: PrintUsage(stdout); return 0;
	case ParseResult::FontReportRequested:
		std::printf("%s\n", Folio::GetCoreVersionString().c_str());
		return 0;
	case ParseResult::Error:
		std::fprintf(stderr, "uipreview:0:0: error: [E-ARG] %s\n", error.c_str());
		PrintUsage(stderr);
		return 2;
	case ParseResult::Ok: break;
	}

	Diagnostics diagnostics;
	diagnostics.SetVerbose(options.verbose);

	try
	{
		App app(options, diagnostics);
		if (!app.Initialise())
		{
			diagnostics.PrintSummary(options.document);
			// A failure to load the document is a document error; anything earlier is a
			// failure to start at all.
			return diagnostics.ErrorCount() > 0 ? (app.GetContext() ? 1 : 3) : 3;
		}

		const int run_result = app.Run();

		// Before the summary and before the exit code, so a finding counts under --strict rather than
		// being printed after the run has already decided it passed.
		if (options.a11y || !options.a11y_report.empty())
			CheckAccessibility(options, app, diagnostics);

		// Always, unlike the accessibility checks: this one costs a tree walk and catches something
		// no screenshot shows, so there is nothing to gain by making it opt in.
		CheckPerformance(options, app, diagnostics);

		// After the checks, because a baker document is expected to trip the full-screen layer
		// warning: producing the texture is the whole point of it.
		if (!options.bakes.empty())
			RunBakes(options, app, diagnostics);

		diagnostics.PrintSummary(options.document);
		const int exit_code = run_result != 0 ? run_result : diagnostics.ComputeExitCode(options.strict);

		if (!options.json_report.empty())
		{
			std::string report_error;
			if (!WriteJsonReport(options.json_report, options, app, diagnostics, exit_code, report_error))
			{
				std::fprintf(stderr, "uipreview:0:0: error: [E-REPORT] %s: %s\n", options.json_report.c_str(),
					report_error.c_str());
				return 4;
			}
		}

		if (!options.metrics_report.empty())
		{
			std::string report_error;
			if (!WriteMetricsReport(options.metrics_report, options, app, diagnostics, report_error))
			{
				std::fprintf(stderr, "uipreview:0:0: error: [E-METRICS] %s: %s\n",
					options.metrics_report.c_str(), report_error.c_str());
				return 4;
			}
		}

		if (!options.a11y_report.empty())
		{
			std::string report_error;
			if (!WriteAccessibilityReport(options.a11y_report, options, app, diagnostics, report_error))
			{
				std::fprintf(stderr, "uipreview:0:0: error: [E-A11Y] %s: %s\n",
					options.a11y_report.c_str(), report_error.c_str());
				return 4;
			}
		}

		if (options.a11y_tree)
			PrintAccessibilityTree(app, stdout);

		if (!options.a11y_tree_report.empty())
		{
			std::string report_error;
			if (!WriteAccessibilityTree(options.a11y_tree_report, options, app, report_error))
			{
				std::fprintf(stderr, "uipreview:0:0: error: [E-A11Y] %s: %s\n",
					options.a11y_tree_report.c_str(), report_error.c_str());
				return 4;
			}
		}

		if (!options.provenance_report.empty())
		{
			std::string report_error;
			if (!WriteProvenanceReport(options.provenance_report, options, app, report_error))
			{
				std::fprintf(stderr, "uipreview:0:0: error: [E-PROVENANCE] %s: %s\n",
					options.provenance_report.c_str(), report_error.c_str());
				return 4;
			}
		}

		return exit_code;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "uipreview:0:0: error: [E-UNHANDLED] %s\n", e.what());
		return 4;
	}

	return diagnostics.ComputeExitCode(options.strict);
}

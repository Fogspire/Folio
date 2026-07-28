#include "App.h"
#include "Diagnostics.h"
#include "JsonReport.h"
#include "Options.h"

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

		return exit_code;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "uipreview:0:0: error: [E-UNHANDLED] %s\n", e.what());
		return 4;
	}

	return diagnostics.ComputeExitCode(options.strict);
}

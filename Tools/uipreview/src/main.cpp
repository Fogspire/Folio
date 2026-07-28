#include "App.h"
#include "Diagnostics.h"
#include "Options.h"

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
		if (run_result != 0)
			return run_result;
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "uipreview:0:0: error: [E-UNHANDLED] %s\n", e.what());
		return 4;
	}

	diagnostics.PrintSummary(options.document);
	return diagnostics.ComputeExitCode(options.strict);
}

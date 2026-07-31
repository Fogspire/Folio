#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioDocumentPath.h"
#include "FolioWorldDocument.h"

#include "Misc/DataValidation.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** A document on disk under the content root, removed however the test leaves. */
	struct FScopedDocument
	{
		FScopedDocument(const TCHAR* Name, const FString& Markup)
			: FScopedDocument(TEXT("ui/"), Name, Markup)
		{
		}

		FScopedDocument(const TCHAR* Directory, const TCHAR* Name, const FString& Markup)
			: Relative(FString(Directory) + Name)
			, Absolute(FPaths::ProjectDir() / Relative)
		{
			FFileHelper::SaveStringToFile(Markup, *Absolute);
		}

		~FScopedDocument()
		{
			IFileManager::Get().Delete(*Absolute, false, true, true);
		}

		FString Relative;
		FString Absolute;
	};

	FString Document(const FString& Head)
	{
		return TEXT("<rml>\n<head>\n<title>t</title>\n") + Head + TEXT("\n</head>\n<body></body>\n</rml>\n");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioDeclaredSizeTest, "Folio.Document.DeclaredSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioDeclaredSizeTest::RunTest(const FString&)
{
	const auto Read = [this](const TCHAR* Name, const FString& Head, FVector2D& OutSize)
	{
		const FScopedDocument Doc(Name, Document(Head));
		return UFolioDocumentPathLibrary::GetDeclaredDocumentSize(Doc.Relative, OutSize);
	};

	FVector2D Size = FVector2D::ZeroVector;

	TestTrue(TEXT("a declared size is read"),
		Read(TEXT("folio_test_a.rml"), TEXT("<meta name=\"folio-size\" content=\"640x360\"/>"), Size));
	TestEqual(TEXT("width"), Size.X, 640.0);
	TestEqual(TEXT("height"), Size.Y, 360.0);

	// Single quotes are as valid as double in the markup, so which one the author reached for must not
	// decide whether this works.
	TestTrue(TEXT("single quotes"),
		Read(TEXT("folio_test_b.rml"), TEXT("<meta name='folio-size' content='800x600'/>"), Size));
	TestEqual(TEXT("width"), Size.X, 800.0);

	TestTrue(TEXT("whitespace around the value"),
		Read(TEXT("folio_test_c.rml"), TEXT("<meta name=\"folio-size\" content=\" 1024 X 640 \"/>"), Size));
	TestEqual(TEXT("width"), Size.X, 1024.0);
	TestEqual(TEXT("height"), Size.Y, 640.0);

	TestFalse(TEXT("a document that says nothing is left alone"),
		Read(TEXT("folio_test_d.rml"), TEXT("<title>no size here</title>"), Size));

	TestFalse(TEXT("a malformed size is refused rather than half read"),
		Read(TEXT("folio_test_e.rml"), TEXT("<meta name=\"folio-size\" content=\"wide\"/>"), Size));

	TestFalse(TEXT("zero is not a size"),
		Read(TEXT("folio_test_f.rml"), TEXT("<meta name=\"folio-size\" content=\"0x480\"/>"), Size));

	// Only the head is read. The same text in the body is markup a template happens to contain, not a
	// declaration about the document.
	{
		const FScopedDocument Doc(TEXT("folio_test_g.rml"),
			TEXT("<rml>\n<head>\n<title>t</title>\n</head>\n")
			TEXT("<body><p>&lt;meta name=\"folio-size\" content=\"111x222\"/&gt;</p></body>\n</rml>\n"));

		TestFalse(TEXT("the body is not searched"),
			UFolioDocumentPathLibrary::GetDeclaredDocumentSize(Doc.Relative, Size));
	}

	TestFalse(TEXT("a document that does not exist"),
		UFolioDocumentPathLibrary::GetDeclaredDocumentSize(TEXT("ui/nothing_here.rml"), Size));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioDocumentOptionsTest, "Folio.Document.Options",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioDocumentOptionsTest::RunTest(const FString&)
{
	const FString Markup = TEXT("<rml><head></head><body></body></rml>\n");

	const FScopedDocument Content(TEXT("folio_test_options.rml"), Markup);
	const FScopedDocument Output(TEXT("Saved/"), TEXT("folio_test_output.rml"), Markup);
	const FScopedDocument Nested(TEXT("Intermediate/"), TEXT("folio_test_nested.rml"), Markup);

	const TArray<FString> Options = UFolioDocumentPathLibrary::GetDocumentOptions();

	TestTrue(TEXT("a document under ui/ is offered"), Options.Contains(Content.Relative));

	// The reason the walk prunes rather than filters: a build directory holds real documents - the
	// vendored RmlUi samples are .rml files - so nothing about a result tells them from the game's.
	TestFalse(TEXT("nothing from Saved/"), Options.Contains(Output.Relative));
	TestFalse(TEXT("nothing from Intermediate/"), Options.Contains(Nested.Relative));

	for (const FString& Option : Options)
	{
		if (Option.Contains(TEXT("/Intermediate/")) || Option.Contains(TEXT("/Binaries/"))
			|| Option.Contains(TEXT("/build/")) || Option.Contains(TEXT("/Saved/")))
		{
			AddError(FString::Printf(TEXT("'%s' is output, not content"), *Option));
		}

		// Folio's own documents are offered under /folio/, so a second copy reached by walking the
		// project is a duplicate with a path that only resolves from this project.
		if (Option.StartsWith(TEXT("Plugins/Folio/")))
		{
			AddError(FString::Printf(TEXT("'%s' should only appear under /folio/"), *Option));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioDocumentValidationTest, "Folio.Document.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioDocumentValidationTest::RunTest(const FString&)
{
	UFolioWorldDocument* Component = NewObject<UFolioWorldDocument>();

	const auto Errors = [Component](const TCHAR* Path)
	{
		FDataValidationContext Context;
		UFolioDocumentPathLibrary::ValidateDocumentPath(*Component, Path, &Context);
		return Context.GetNumErrors();
	};

	// The mount begins with a slash and is not a path on this machine, which is the whole reason
	// this needs a test: every document Folio ships is named this way.
	TestEqual(TEXT("a /folio/ mount is portable"), Errors(TEXT("/folio/examples/mask_image.rml")), 0);

	TestTrue(TEXT("a path on this machine is not"),
		Errors(TEXT("X:/Kaboom/Kaboom/ui/screens/hud.rml")) > 0);
	TestTrue(TEXT("a document that does not exist is reported"),
		Errors(TEXT("ui/screens/nothing_here.rml")) > 0);

	// Unplaced, so an empty path is a component waiting to be given one from code.
	TestEqual(TEXT("an empty path on an unplaced component"), Errors(TEXT("")), 0);

	return true;
}

#endif

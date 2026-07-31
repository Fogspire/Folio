#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioDocumentPath.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** A document on disk under the content root, removed however the test leaves. */
	struct FScopedDocument
	{
		FScopedDocument(const TCHAR* Name, const FString& Markup)
			: Relative(FString(TEXT("ui/")) + Name)
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

#endif

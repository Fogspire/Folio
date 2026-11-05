#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioDocumentHost.h"
#include "FolioModule.h"
#include "SFolioCanvas.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <RmlUi/Core/ElementDocument.h>

namespace
{
	/** A document on disk under the content root, removed however the test leaves. */
	struct FScopedSizeDocument
	{
		FScopedSizeDocument(const TCHAR* Name, const FString& Body)
			: Relative(FString(TEXT("UI/")) + Name)
			, Absolute(FPaths::ProjectDir() / Relative)
		{
			FFileHelper::SaveStringToFile(
				TEXT("<rml>\n<head>\n<title>t</title>\n<style>\n")
				TEXT("body, div, p { display: block; }\n")
				TEXT("body { margin: 0; padding: 10px; font-family: Inter; font-size: 20px; }\n")
				TEXT("</style>\n</head>\n<body>") + Body + TEXT("</body>\n</rml>\n"),
				*Absolute);
		}

		~FScopedSizeDocument()
		{
			IFileManager::Get().Delete(*Absolute, false, true, true);
		}

		FString Relative;
		FString Absolute;
	};
}

/**
 * Size to content measures the body, not the context: a fixed block inside it gives the same size in
 * any context large enough, a narrow context wraps text, and turning it off gives the body back its
 * stylesheet width.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioSizeToContentTest, "Folio.Document.SizeToContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioSizeToContentTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised"));
		return false;
	}

	{
		const FScopedSizeDocument Doc(TEXT("folio_test_size_block.rml"),
			TEXT("<div style=\"width: 300px; height: 40px;\"></div>"));

		FFolioDocumentHost Host;
		if (!TestTrue(TEXT("the host is created"), Host.Create(TEXT("test"), FIntPoint(1920, 1080))))
		{
			return false;
		}

		Host.LoadDocument(Doc.Relative);
		Host.SetSizeToContent(true);
		Host.SetSize(FIntPoint(8192, 8192));
		Host.Update(0.0f, 1.0f, 1.0f);

		TestEqual(TEXT("the width is the block and the padding"), Host.GetContentSize().X, 320.0f, 0.5f);
		TestEqual(TEXT("the height is the block and the padding"), Host.GetContentSize().Y, 60.0f, 0.5f);

		Host.SetSize(FIntPoint(1000, 1000));
		Host.Update(0.0f, 1.0f, 1.0f);
		TestEqual(TEXT("a different context measures the same"), Host.GetContentSize().X, 320.0f, 0.5f);

		Host.SetSizeToContent(false);
		Host.Update(0.0f, 1.0f, 1.0f);
		TestEqual(TEXT("off, the content size is cleared"), Host.GetContentSize().X, 0.0f);

		Rml::ElementDocument* Body = Host.GetDocument();
		if (TestNotNull(TEXT("the document loaded"), Body))
		{
			TestEqual(TEXT("off, the body fills the context again"),
				Body->GetBox().GetSize(Rml::BoxArea::Border).x, 1000.0f, 0.5f);
		}

		Host.Destroy();
	}

	{
		const FScopedSizeDocument Doc(TEXT("folio_test_size_text.rml"),
			TEXT("<p>A line long enough that a narrow width has to wrap it onto several lines.</p>"));

		FFolioDocumentHost Host;
		if (!TestTrue(TEXT("the host is created"), Host.Create(TEXT("test"), FIntPoint(1920, 1080))))
		{
			return false;
		}

		Host.LoadDocument(Doc.Relative);
		Host.SetSizeToContent(true);
		Host.SetSize(FIntPoint(8192, 8192));
		Host.Update(0.0f, 1.0f, 1.0f);
		const FVector2f OneLine = Host.GetContentSize();

		Host.SetSize(FIntPoint(200, 8192));
		Host.Update(0.0f, 1.0f, 1.0f);
		const FVector2f Wrapped = Host.GetContentSize();

		TestTrue(TEXT("one line is wider than the limit"), OneLine.X > 200.0f);
		TestTrue(TEXT("a limit caps the width"), Wrapped.X <= 200.5f);
		TestTrue(TEXT("and the text wraps taller"), Wrapped.Y > OneLine.Y * 2.0f);

		Host.Destroy();
	}

	{
		const FScopedSizeDocument Doc(TEXT("folio_test_size_canvas.rml"),
			TEXT("<div style=\"width: 300px; height: 40px;\"></div>"));

		const TSharedRef<SFolioCanvas> Canvas = SNew(SFolioCanvas)
			.DesiredSize(FVector2D(640.0, 480.0))
			.SizeToContent(true);
		Canvas->LoadDocument(Doc.Relative);

		TestEqual(TEXT("before a layout, Desired Size is reported"), Canvas->ComputeDesiredSize(1.0f), FVector2D(640.0, 480.0));

		// A Slate scale of 2: the content is measured in pixels and reported in Slate units, so half.
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(1000.0, 1000.0), FSlateLayoutTransform(2.0f));
		Canvas->Tick(Geometry, 0.0, 0.0f);

		TestEqual(TEXT("the content is reported in Slate units"), Canvas->ComputeDesiredSize(1.0f), FVector2D(160.0, 30.0));

		Canvas->SetSizeToContent(false, 0.0f);
		TestEqual(TEXT("off, Desired Size is reported again"), Canvas->ComputeDesiredSize(1.0f), FVector2D(640.0, 480.0));
	}

	return true;
}

#endif

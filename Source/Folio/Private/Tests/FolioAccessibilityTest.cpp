#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioAccessibility.h"
#include "FolioModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	/**
	 * A context and one document in it, removed however the test leaves.
	 *
	 * RmlUi keeps contexts in a global registry, so one left behind by a failing test is still there
	 * for the next one.
	 */
	struct FScopedA11yDocument
	{
		FScopedA11yDocument(const Rml::String& InName, const Rml::String& A11yMarkup)
			: Name(InName)
			, Context(Rml::CreateContext(InName, Rml::Vector2i(640, 480)))
		{
			if (Context)
			{
				Document = Context->LoadDocumentFromMemory(A11yMarkup);
				if (Document)
				{
					Document->Show();
					Context->Update();
				}
			}
		}

		~FScopedA11yDocument()
		{
			if (Context)
			{
				Rml::RemoveContext(Name);
			}
		}

		Rml::String Name;
		Rml::Context* Context = nullptr;
		Rml::ElementDocument* Document = nullptr;
	};

	/**
	 * Every box is sized in the markup rather than by its content.
	 *
	 * A test that depended on text metrics would depend on which fonts the project happens to have
	 * registered, and an element that measures zero is an element the tree correctly drops - so the
	 * failure would read as a broken walk rather than a missing typeface.
	 */
	const Rml::String A11yMarkup = R"(<rml>
<head><title>Settings</title><style>
	body { width: 640px; height: 480px; }
	div, button, input, span { display: block; width: 200px; height: 40px; }
	.hidden-thing { width: 100px; height: 10px; }
	.switch input { position: absolute; width: 100%; height: 100%; opacity: 0; tab-index: auto; }
</style></head>
<body>
	<div folio-label="Audio">
		<div>Volume</div>
		<button id="mute" folio-label="Mute" folio-description="Silences everything">M</button>
		<div class="switch">
			<input type="checkbox" id="subs" checked/><span>Subtitles</span>
		</div>
	</div>
	<div class="hidden-thing" folio-hidden>decoration</div>
	<button id="apply">Apply</button>
	<button id="revert" disabled>Revert</button>
</body>
</rml>)";
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioSemanticTreeTest, "Folio.Accessibility.SemanticTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioSemanticTreeTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no document can be loaded."));
		return false;
	}

	FScopedA11yDocument Scope("FolioTest.Accessibility", A11yMarkup);
	if (!TestNotNull(TEXT("document"), Scope.Document))
	{
		return false;
	}

	const Folio::FSemanticNode Root = Folio::BuildSemanticTree(Scope.Document);

	TestEqual(TEXT("the document is named by its title"), FString(Root.Name.c_str()), TEXT("Settings"));

	TArray<FString> Lines;
	Folio::VisitSemanticTree(Root, [&Lines](const Folio::FSemanticNode& Node, const int32 Depth)
	{
		Lines.Add(FString::ChrN(Depth * 2, TEXT(' ')) + ANSI_TO_TCHAR(Folio::RoleName(Node.Role))
			+ TEXT(" ") + FString(Node.Name.c_str()));
	});

	const TArray<FString> Expected = {
		TEXT("group Settings"),
		TEXT("  group Audio"),
		TEXT("    text Volume"),
		TEXT("    button Mute"),
		// The caption beside the checkbox names it and is not repeated as text of its own, and the
		// input is in the tree despite being drawn at zero opacity - it is still what a click hits.
		TEXT("    checkbox Subtitles"),
		// folio-hidden takes the decoration and its text with it.
		TEXT("  button Apply"),
		TEXT("  button Revert"),
	};

	TestEqual(TEXT("node count"), Lines.Num(), Expected.Num());

	for (int32 Index = 0; Index < FMath::Min(Lines.Num(), Expected.Num()); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("line %d"), Index), Lines[Index], Expected[Index]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioAccessibleWidgetsTest, "Folio.Accessibility.Widgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioAccessibleWidgetsTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no document can be loaded."));
		return false;
	}

	FScopedA11yDocument Scope("FolioTest.AccessibilityWidgets", A11yMarkup);
	if (!TestNotNull(TEXT("document"), Scope.Document))
	{
		return false;
	}

	FFolioAccessibilityTree Tree;
	TestTrue(TEXT("the first build is a change"), Tree.Refresh(Scope.Document, 1.0f));
	TestFalse(TEXT("and a document that has not moved rebuilds to the same widgets"),
		Tree.Refresh(Scope.Document, 1.0f));

	TArray<FString> Lines;
	Tree.Describe(Lines);

	const TArray<FString> Expected = {
		TEXT("group \"Settings\""),
		TEXT("  group \"Audio\""),
		TEXT("    text \"Volume\""),
		TEXT("    button \"Mute\""),
		TEXT("    checkbox \"Subtitles\" [checked]"),
		TEXT("  button \"Apply\""),
		TEXT("  button \"Revert\" [disabled]"),
	};

	TestEqual(TEXT("widget count"), Lines.Num(), Expected.Num());

	for (int32 Index = 0; Index < FMath::Min(Lines.Num(), Expected.Num()); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("line %d"), Index), Lines[Index], Expected[Index]);
	}

	// The widget standing for an element has to be where the element is, because that rectangle is
	// what the platform hit tests and what a screen reader's cursor follows.
	if (const TSharedPtr<SFolioAccessibleElement> Root = Tree.GetRoot())
	{
		TestEqual(TEXT("the root covers the document"), Root->ComputeDesiredSize(1.0f),
			FVector2D(640.0, 480.0));
	}

	Tree.Clear();
	TestTrue(TEXT("clearing drops the tree"), Tree.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

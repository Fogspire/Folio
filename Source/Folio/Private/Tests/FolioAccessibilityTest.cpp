#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioAccessibility.h"
#include "FolioAccessibilityLibrary.h"
#include "FolioAccessibilityReview.h"
#include "FolioDocumentHost.h"
#include "FolioModule.h"
#include "FolioSpeech.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

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

	/** The same shape with a field in it, for the tests that are about what is said rather than built. */
	const Rml::String A11ySpeechMarkup = R"(<rml>
<head><title>Profile</title><style>
	body { width: 640px; height: 480px; }
	div, button, input { display: block; width: 200px; height: 40px; }
	button, input { tab-index: auto; }
</style></head>
<body>
	<div folio-label="Account">
		<button id="save" folio-label="Save">S</button>
		<input type="text" id="name" folio-label="Name"/>
	</div>
</body>
</rml>)";

	/**
	 * A voice that writes down what it was told to say.
	 *
	 * The reason `FSpeech` takes a back end at all rather than compiling one in: a test can be the
	 * platform, and what the reader says is then an assertion rather than something a person has to
	 * sit and listen to.
	 */
	class FRecordingVoice final : public Folio::ISpeechBackend
	{
	public:
		virtual bool Start() override { return true; }
		virtual void Stop() override {}
		virtual void Speak(const FString& Text) override { Lines->Add(Text); }
		virtual void Silence() override {}

		// Never busy, so the queue drains a line per pump and a test never waits on a voice.
		virtual bool IsSpeaking() const override { return false; }
		virtual void SetRate(int32) override {}
		virtual void SetVolume(int32) override {}

		TArray<FString>* Lines = nullptr;
	};

	/** Installs the recording voice and turns the reader on, and puts both back however the test ends. */
	struct FScopedReader
	{
		explicit FScopedReader(TArray<FString>& InLines)
		{
			TUniquePtr<FRecordingVoice> Voice = MakeUnique<FRecordingVoice>();
			Voice->Lines = &InLines;
			Folio::FSpeech::SetBackend(MoveTemp(Voice));

			FFolioAccessibilityTree::SetScreenReader(true);
			InLines.Reset();
		}

		~FScopedReader()
		{
			FFolioAccessibilityTree::SetScreenReader(false);
			Folio::FSpeech::SetBackend(nullptr);
		}
	};

	/** A document on disk under the content root, so a host can load one by path. */
	struct FScopedA11yFile
	{
		FScopedA11yFile(const TCHAR* Name, const FString& Markup)
			: Relative(FString(TEXT("UI/")) + Name)
			, Absolute(FPaths::ProjectDir() / Relative)
		{
			FFileHelper::SaveStringToFile(Markup, *Absolute);
		}

		~FScopedA11yFile()
		{
			IFileManager::Get().Delete(*Absolute, false, true, true);
		}

		FString Relative;
		FString Absolute;
	};
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioReaderSpeechTest, "Folio.Accessibility.Speech",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioReaderSpeechTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no document can be loaded."));
		return false;
	}

	FScopedA11yDocument Scope("FolioTest.AccessibilitySpeech", A11ySpeechMarkup);
	if (!TestNotNull(TEXT("document"), Scope.Document))
	{
		return false;
	}

	TArray<FString> Said;
	FScopedReader Reader(Said);

	FFolioAccessibilityTree Tree;
	Tree.Refresh(Scope.Document, 1.0f);

	// Moving onto a control names the panel it is in, once.
	Scope.Document->GetElementById("save")->Focus();
	Tree.SyncFocus(Scope.Context);
	TestEqual(TEXT("a focused button reads out name, kind and group"),
		Said.Num() > 0 ? Said.Last() : FString(), TEXT("Account, Save, button"));

	Scope.Document->GetElementById("name")->Focus();
	Tree.SyncFocus(Scope.Context);
	TestEqual(TEXT("and the panel is not named again on the next row"),
		Said.Num() > 0 ? Said.Last() : FString(), TEXT("Name, edit"));

	// The field's contents as they change. The first read is silent, because the focus change above
	// has already said what is in it.
	Rml::ElementFormControl* Field =
		rmlui_dynamic_cast<Rml::ElementFormControl*>(Scope.Document->GetElementById("name"));
	if (!TestNotNull(TEXT("the field is a form control"), Field))
	{
		return false;
	}

	Tree.SyncEditing(Scope.Context);

	Said.Reset();
	Field->SetValue("a");
	Tree.SyncEditing(Scope.Context);
	TestEqual(TEXT("one typed character is echoed"), Said.Num() > 0 ? Said.Last() : FString(), TEXT("a"));

	Said.Reset();
	Field->SetValue("a,");
	Tree.SyncEditing(Scope.Context);
	TestEqual(TEXT("and punctuation is named, because a bare comma is silence"),
		Said.Num() > 0 ? Said.Last() : FString(), TEXT("comma"));

	Said.Reset();
	Field->SetValue("a");
	Tree.SyncEditing(Scope.Context);
	TestEqual(TEXT("a deletion says what went"), Said.Num() > 0 ? Said.Last() : FString(),
		TEXT("deleted comma"));

	Said.Reset();
	Field->SetValue("");
	Tree.SyncEditing(Scope.Context);

	// "blank" is the result rather than the keystroke, so it waits its turn behind the deletion.
	Folio::FSpeech::Pump();
	TestTrue(TEXT("and an emptied field says so, which nothing else could tell a player"),
		Said.Contains(TEXT("blank")));

	// What the backlog fix is: forty presses on one value leave one line waiting, not forty.
	Said.Reset();
	for (int32 Step = 0; Step < 40; ++Step)
	{
		Folio::FSpeech::Say(FString::Printf(TEXT("%d"), Step), Folio::ESpeechPriority::Polite,
			TEXT("hud.scale"));
	}

	TestEqual(TEXT("a value that keeps changing queues once"), Folio::FSpeech::NumQueued(), 1);

	Folio::FSpeech::Pump();
	TestEqual(TEXT("and what is said is the last one, not the first"),
		Said.Num() > 0 ? Said.Last() : FString(), TEXT("39"));

	// A focus change is not a queue entry. It is the queue being wrong.
	Folio::FSpeech::Say(TEXT("a"), Folio::ESpeechPriority::Polite);
	Folio::FSpeech::Say(TEXT("b"), Folio::ESpeechPriority::Polite);
	Folio::FSpeech::Say(TEXT("moved"), Folio::ESpeechPriority::Assertive);
	TestEqual(TEXT("an assertive line drops what was waiting"), Folio::FSpeech::NumQueued(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioReviewCursorTest, "Folio.Accessibility.Review",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioReviewCursorTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no document can be loaded."));
		return false;
	}

	const FScopedA11yFile File(TEXT("folio_test_review.rml"), FString(A11ySpeechMarkup.c_str()));

	// A host rather than a bare context, because the review cursor reads every live document and a
	// document only becomes one by being loaded into a host. That is also what makes it able to read a
	// document in the world, which has no widget for a platform reader to find.
	FFolioDocumentHost Host;
	if (!TestTrue(TEXT("the host is created"), Host.Create(TEXT("review"), FIntPoint(640, 480))))
	{
		return false;
	}

	Host.LoadDocument(File.Relative);
	Host.Update(0.0f, 1.0f, 1.0f);

	if (!TestNotNull(TEXT("document"), Host.GetDocument()))
	{
		Host.Destroy();
		return false;
	}

	TArray<FString> Said;
	FScopedReader Reader(Said);

	// Walked to rather than assumed to be first: the cursor covers every live document, and an editor
	// running this has no obligation to have closed the others.
	FString Line = FFolioReview::Move(FFolioReview::EMove::First);
	for (int32 Guard = 0; Guard < 200 && Line != TEXT("Profile"); ++Guard)
	{
		Line = FFolioReview::Move(FFolioReview::EMove::Next);
	}

	if (!TestEqual(TEXT("the screen is reachable, named by its title"), Line, TEXT("Profile")))
	{
		Host.Destroy();
		return false;
	}

	// The screen is named on the way into the panel, and the panel on the way into what is in it, and
	// neither is repeated after that.
	TestEqual(TEXT("then the panel, under the screen's name"),
		FFolioReview::Move(FFolioReview::EMove::Next), TEXT("Profile, Account"));

	TestEqual(TEXT("then what is in it, under the panel's"),
		FFolioReview::Move(FFolioReview::EMove::Next), TEXT("Account, Save, button"));

	TestEqual(TEXT("and the last thing said comes back on repeat"),
		FFolioReview::Move(FFolioReview::EMove::Repeat), TEXT("Account, Save, button"));

	TestEqual(TEXT("moving back re-enters the panel, so it is named again"),
		FFolioReview::Move(FFolioReview::EMove::Previous), TEXT("Profile, Account"));

	// Nothing above the first line, and a player is told rather than left pressing a key that does
	// nothing.
	while (FFolioReview::Move(FFolioReview::EMove::Previous) != TEXT("top"))
	{
	}

	TestEqual(TEXT("the top edge is announced"), FFolioReview::Move(FFolioReview::EMove::Previous),
		TEXT("top"));

	// The half of this that is not about accessibility: reading a screen from game code needs nothing
	// turned on, because it walks the document rather than the widgets built for a platform.
	const TArray<FString> Screen = UFolioAccessibilityLibrary::DescribeScreen();
	TestTrue(TEXT("a screen can be read without a screen reader"), Screen.Contains(TEXT("Save, button")));

	Host.GetDocument()->GetElementById("save")->Focus();
	Host.Update(0.0f, 1.0f, 1.0f);
	TestEqual(TEXT("and what has focus can be asked for the same way"),
		UFolioAccessibilityLibrary::GetFocusedControl(), TEXT("Save, button"));

	// Reading the field without moving through it, which is what someone correcting a mistake needs.
	Rml::ElementFormControl* Field =
		rmlui_dynamic_cast<Rml::ElementFormControl*>(Host.GetDocument()->GetElementById("name"));
	if (!TestNotNull(TEXT("the field is a form control"), Field))
	{
		Host.Destroy();
		return false;
	}

	Field->SetValue("Vega Seven");
	Host.GetDocument()->GetElementById("name")->Focus();
	Host.Update(0.0f, 1.0f, 1.0f);

	if (auto* Input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(Host.GetDocument()->GetElementById("name")))
	{
		Input->SetSelectionRange(2, 2);
	}

	TestEqual(TEXT("the word the caret is in is read back"), FFolioAccessibilityTree::ReadEditWord(), TEXT("Vega"));
	TestEqual(TEXT("and spelled out on request"), FFolioAccessibilityTree::SpellEditWord(), TEXT("V, e, g, a"));
	TestEqual(TEXT("and the whole line"), FFolioAccessibilityTree::ReadEditLine(), TEXT("Vega Seven"));

	// Punctuation is its own axis: a path wants every symbol, a name wants none.
	UFolioAccessibilityLibrary::SetPunctuation(EFolioPunctuation::All);
	Field->SetValue("a/b");
	if (auto* Input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(Host.GetDocument()->GetElementById("name")))
	{
		Input->SetSelectionRange(1, 1);
	}

	TestEqual(TEXT("every symbol is named at the highest level"), FFolioAccessibilityTree::ReadEditWord(),
		TEXT("a, slash, b"));

	UFolioAccessibilityLibrary::SetPunctuation(EFolioPunctuation::Some);

	Host.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioTreeRequestTest, "Folio.Accessibility.TreeRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioTreeRequestTest::RunTest(const FString&)
{
	// Whatever the machine running this happens to have listening. The assertions are about what a
	// request changes, not about what it starts from.
	const bool bAlready = UFolioAccessibilityLibrary::IsDocumentTreeBuilt();

	{
		const FFolioDocumentTreeRequest Request(TEXT("test"));
		TestTrue(TEXT("a request builds the tree"), UFolioAccessibilityLibrary::IsDocumentTreeBuilt());

		{
			// Counted, so the inner release does not drop the outer system's tree from under it.
			const FFolioDocumentTreeRequest Nested(TEXT("test"));
			TestTrue(TEXT("and so does a second one"),
				UFolioAccessibilityLibrary::IsDocumentTreeBuilt());
		}

		TestTrue(TEXT("which the first one still holds open"),
			UFolioAccessibilityLibrary::IsDocumentTreeBuilt());
	}

	TestEqual(TEXT("and releasing the last puts it back"),
		UFolioAccessibilityLibrary::IsDocumentTreeBuilt(), bAlready);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

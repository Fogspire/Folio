#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioNavigation.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	/** A settings-style list: plain rows, and a stepper row whose focus lives on the range input filling it. */
	const Rml::String WrapMarkup = R"(<rml>
<head><style>
	body { width: 640px; height: 480px; }
	div { display: block; width: 200px; height: 40px; }
	.list { height: 200px; }
	.row { position: relative; tab-index: auto; nav: vertical; }
	.row.stepper { tab-index: none; }
	.row input { display: block; position: absolute; left: 0px; top: 0px; width: 100%; height: 100%; tab-index: auto; nav: vertical; }
	.row.off { tab-index: none; nav: none; }
	.gate { position: absolute; top: 0px; left: 300px; tab-index: auto; }
	#last { left: 520px; }
	#first { nav-up: none; nav-down: #last; }
	#last { nav-up: #first; nav-down: none; }
</style></head>
<body>
	<div class="list">
		<div class="row" id="top"/>
		<div class="row stepper"><input type="range" id="slider" min="0" max="1" step="0.1"/></div>
		<div class="row" id="bottom"/>
		<div class="row off" id="off"/>
	</div>
	<div class="gate" id="first"/>
	<div class="gate" id="last"/>
</body>
</rml>)";

	struct FScopedWrapDocument
	{
		explicit FScopedWrapDocument(const Rml::String& InName)
			: Name(InName)
			, Context(Rml::CreateContext(InName, Rml::Vector2i(640, 480)))
		{
			if (Context)
			{
				Document = Context->LoadDocumentFromMemory(WrapMarkup);
				if (Document)
				{
					Document->Show();
					Context->Update();
				}
			}
		}

		~FScopedWrapDocument()
		{
			if (Context)
			{
				Rml::RemoveContext(Name);
			}
		}

		/** Presses a direction the way Folio's widgets do, and returns the id that has focus after. */
		FString Press(const Rml::Input::KeyIdentifier Key)
		{
			const Rml::Element* Before = Context->GetFocusElement();
			Context->ProcessKeyDown(Key, 0);
			Folio::WrapNavigation(*Context, Key, Before);
			Context->ProcessKeyUp(Key, 0);
			Context->Update();
			const Rml::Element* After = Context->GetFocusElement();
			return After ? FString(UTF8_TO_TCHAR(After->GetId().c_str())) : FString();
		}

		Rml::String Name;
		Rml::Context* Context = nullptr;
		Rml::ElementDocument* Document = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioNavigationWrapTest, "Folio.Navigation.Wrap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFolioNavigationWrapTest::RunTest(const FString& Parameters)
{
	using namespace Rml::Input;

	FScopedWrapDocument Doc("folio-nav-wrap-test");
	if (!TestNotNull(TEXT("document loads"), Doc.Document))
	{
		return false;
	}

	Doc.Document->GetElementById("top")->Focus(true);
	TestEqual(TEXT("down moves normally"), Doc.Press(KI_DOWN), FString(TEXT("slider")));
	TestEqual(TEXT("down onto the last live row"), Doc.Press(KI_DOWN), FString(TEXT("bottom")));
	TestEqual(TEXT("down from the bottom wraps to the top"), Doc.Press(KI_DOWN), FString(TEXT("top")));
	TestEqual(TEXT("up from the top wraps to the bottom"), Doc.Press(KI_UP), FString(TEXT("bottom")));
	TestEqual(TEXT("left on a vertical list does not wrap"), Doc.Press(KI_LEFT), FString(TEXT("bottom")));

	Doc.Document->GetElementById("first")->Focus(true);
	TestEqual(TEXT("an explicit nav-up: none stays put"), Doc.Press(KI_UP), FString(TEXT("first")));
	return true;
}

#endif

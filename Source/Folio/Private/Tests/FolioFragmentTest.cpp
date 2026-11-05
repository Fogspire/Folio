#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioDocumentHost.h"
#include "FolioModule.h"
#include "FolioTestViewModel.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace
{
	const TCHAR* FragmentTestDirectory = TEXT("Saved/FolioTest/Fragments");

	/** Writes a test document under the content root and returns the path a host loads it by. */
	FString WriteTestDocument(const TCHAR* Name, const TCHAR* Markup)
	{
		const FString Relative = FString(FragmentTestDirectory) / Name;
		FFileHelper::SaveStringToFile(Markup, *(FFolioModule::Get().GetContentRoot() / Relative));
		return Relative;
	}

	Rml::Element* FindById(const FFolioDocumentHost& Host, const char* Id)
	{
		Rml::ElementDocument* Document = Host.GetDocument();
		return Document ? Document->GetElementById(Id) : nullptr;
	}

	FString InnerText(Rml::Element* Element)
	{
		return Element ? FString(UTF8_TO_TCHAR(Element->GetInnerRML().c_str())) : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioFragmentHostTest, "Folio.Fragments.Host",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioFragmentHostTest::RunTest(const FString&)
{
	if (!FFolioModule::IsAvailable() || !FFolioModule::Get().IsRmlInitialised())
	{
		AddError(TEXT("RmlUi is not initialised, so no document can load."));
		return false;
	}

	const FString HostPath = WriteTestDocument(TEXT("host.rml"), TEXT(
		"<rml><head><style>body { width: 100%; height: 100%; } div { display: block; }</style></head>"
		"<body><div id=\"slot\"></div></body></rml>"));

	const FString FragmentPath = WriteTestDocument(TEXT("fragment.rml"), TEXT(
		"<rml><head><link type=\"text/rcss\" href=\"fragment.rcss\"/></head>"
		"<body class=\"frag\" data-model=\"fragtest\"><p id=\"score\">{{ Score }}</p></body></rml>"));

	WriteTestDocument(TEXT("fragment.rcss"), TEXT(".frag { width: 37px; }"));

	TStrongObjectPtr<UFolioTestPlainModel> Model(NewObject<UFolioTestPlainModel>());
	Model->Score = 42;

	FFolioDocumentHost Host;
	if (!TestTrue(TEXT("host created"), Host.Create(TEXT("fragment_test"), FIntPoint(320, 240))))
	{
		return false;
	}

	// Added before the document exists, which is the ordinary order from a widget being built.
	Host.AddFragment(1, TEXT("slot"), FragmentPath, Model.Get(), true);
	Host.LoadDocument(HostPath);
	Host.Update(0.016f, 1.0f, 1.0f);

	TestEqual(TEXT("the fragment binds its own model"), InnerText(FindById(Host, "score")), FString(TEXT("42")));
	TestTrue(TEXT("its model is bound"), Host.HasObjectModel(TEXT("fragtest")));

	Rml::Element* Slot = FindById(Host, "slot");
	Rml::Element* Wrapper = Slot && Slot->GetNumChildren() > 0 ? Slot->GetChild(0) : nullptr;
	if (TestNotNull(TEXT("the fragment is placed in its slot"), Wrapper))
	{
		TestTrue(TEXT("its element carries the body's class"), Wrapper->IsClassSet("frag"));
		TestEqual(TEXT("its linked stylesheet applies"), Wrapper->GetClientWidth(), 37.0f);
	}

	Host.ReloadStyleSheets();
	Host.Update(0.016f, 1.0f, 1.0f);
	Wrapper = Slot ? Slot->GetChild(0) : nullptr;
	TestTrue(TEXT("its stylesheet survives a stylesheet reload"), Wrapper && Wrapper->GetClientWidth() == 37.0f);

	Host.ReloadDocument();
	Host.Update(0.016f, 1.0f, 1.0f);
	TestEqual(TEXT("a rebuilt document gets it back"), InnerText(FindById(Host, "score")), FString(TEXT("42")));

	TestTrue(TEXT("removing a known handle succeeds"), Host.RemoveFragment(1));
	Host.Update(0.016f, 1.0f, 1.0f);
	TestNull(TEXT("its markup is gone"), FindById(Host, "score"));
	TestFalse(TEXT("its model is released"), Host.HasObjectModel(TEXT("fragtest")));
	TestFalse(TEXT("removing it twice fails"), Host.RemoveFragment(1));

	Host.Destroy();
	IFileManager::Get().DeleteDirectory(*(FFolioModule::Get().GetContentRoot() / FragmentTestDirectory), false, true);
	return true;
}

#endif

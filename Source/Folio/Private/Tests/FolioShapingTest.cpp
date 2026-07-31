#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "FolioCore/FolioFontEngine.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

/**
 * That the engine build is actually shaping, which nothing else would notice.
 *
 * Text renders either way, and the difference between a shaped run and a per-codepoint one is a
 * fraction of an em per pair. The plugin links the engine's HarfBuzz, so this is also the check
 * that says the library is present and reachable from this module rather than only from Slate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioShapingTest, "Folio.Font.Shaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioShapingTest::RunTest(const FString&)
{
	if (!TestTrue(TEXT("this build has a shaper"), Folio::FFolioFontEngine::IsComplexShapingAvailable()))
	{
		return false;
	}

	Folio::FFolioFontEngine* Engine = FFolioModule::IsAvailable() ? FFolioModule::Get().GetFontEngine() : nullptr;
	if (!Engine)
	{
		AddError(TEXT("no font engine; RmlUi failed to start"));
		return false;
	}

	TestTrue(TEXT("the report names the shaper"),
		FString(Engine->GetReport().c_str()).Contains(TEXT("shaper=harfbuzz")));

	// A face of this test's own, under a family nothing else uses, so the result does not depend
	// on which fonts the game happens to ship. Inter keeps its kerning in GPOS and has no legacy
	// `kern` table, which is exactly the case a shaper exists to cover.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Folio"));
	if (!Plugin)
	{
		AddError(TEXT("the Folio plugin is not registered"));
		return false;
	}

	const FString FontFile = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir())
		/ TEXT("fonts") / TEXT("Inter-Variable.ttf");

	if (!TestTrue(TEXT("the test face registers"),
		Engine->RegisterVariableFace(Folio::ToRmlString(FontFile), "folio-shaping-test",
			Rml::Style::FontStyle::Normal, {})))
	{
		return false;
	}

	const Rml::FontFaceHandle Handle = Engine->GetFontFaceHandle("folio-shaping-test",
		Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 48);
	if (!TestTrue(TEXT("the test face resolves to a handle"), Handle != 0))
	{
		return false;
	}

	const Rml::String Language;
	const Rml::TextShapingContext Kerned{Language, Rml::Style::Direction::Ltr, Rml::Style::FontKerning::Auto, 0.0f};
	const Rml::TextShapingContext Unkerned{Language, Rml::Style::Direction::Ltr, Rml::Style::FontKerning::None, 0.0f};

	const Rml::String Pairs = "AV To Ta Wo LT";
	const int32 WidthKerned = Engine->GetStringWidth(Handle, Pairs, Kerned, Rml::Character::Null);
	const int32 WidthUnkerned = Engine->GetStringWidth(Handle, Pairs, Unkerned, Rml::Character::Null);

	TestTrue(TEXT("the string measures at all"), WidthUnkerned > 0);
	TestTrue(TEXT("GPOS kerning reaches the glyphs"), WidthKerned < WidthUnkerned);

	return true;
}

#endif

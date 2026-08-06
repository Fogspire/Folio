#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioModule.h"
#include "FolioRmlUi.h"

#include "FolioCore/FolioFontEngine.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

/**
 * That a character the requested face has no glyph for is drawn by a face that does.
 *
 * Without it the character resolves to that face's .notdef, which in most of the faces here is
 * blank and correctly sized, so the word simply comes out a letter short. Nothing warns, and the
 * width is plausible either way, so a missing glyph only ever shows up in a render.
 *
 * The widths are compared against the face that owns the glyph rather than against zero for that
 * reason: .notdef has an advance too, and any test that only asks whether the string measured
 * would pass just as happily with no fallback at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioGlyphFallbackTest, "Folio.Font.GlyphFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioGlyphFallbackTest::RunTest(const FString&)
{
	Folio::FFolioFontEngine* Engine = FFolioModule::IsAvailable() ? FFolioModule::Get().GetFontEngine() : nullptr;
	if (!Engine)
	{
		AddError(TEXT("no font engine; RmlUi failed to start"));
		return false;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Folio"));
	if (!Plugin)
	{
		AddError(TEXT("the Folio plugin is not registered"));
		return false;
	}

	const FString Fonts = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()) / TEXT("fonts");

	// Two faces of this test's own, so the result does not depend on which fonts the game ships.
	// Roboto Mono covers Latin-1 and stops; Inter carries the rest of the Latin blocks, so the
	// pair straddles a coverage edge without either being a contrived file.
	if (!TestTrue(TEXT("the narrow face registers"),
			Engine->RegisterVariableFace(Folio::ToRmlString(Fonts / TEXT("RobotoMono-Variable.ttf")),
				"folio-fallback-narrow", Rml::Style::FontStyle::Normal, {}))
		|| !TestTrue(TEXT("the wide face registers"),
			Engine->RegisterVariableFace(Folio::ToRmlString(Fonts / TEXT("Inter-Variable.ttf")),
				"folio-fallback-wide", Rml::Style::FontStyle::Normal, {})))
	{
		return false;
	}

	const auto HandleFor = [Engine](const char* Family)
	{
		return Engine->GetFontFaceHandle(Family, Rml::Style::FontStyle::Normal,
			Rml::Style::FontWeight::Normal, 48);
	};

	const Rml::FontFaceHandle Narrow = HandleFor("folio-fallback-narrow");
	const Rml::FontFaceHandle Wide = HandleFor("folio-fallback-wide");
	if (!TestTrue(TEXT("both faces resolve to handles"), Narrow != 0 && Wide != 0))
	{
		return false;
	}

	const Rml::String Language;
	const Rml::TextShapingContext Context{Language, Rml::Style::Direction::Ltr,
		Rml::Style::FontKerning::Auto, 0.0f};

	const auto Measure = [&](Rml::FontFaceHandle Handle, const Rml::String& Text)
	{
		return Engine->GetStringWidth(Handle, Text, Context, Rml::Character::Null);
	};

	// Escaped rather than written out, so nothing that reformats this file can re-encode the very
	// characters the test is about.
	// U+018F, which Inter has and Roboto Mono does not.
	const Rml::String Missing = "\xC6\x8F";
	// U+F0110, private use, which nothing here has.
	const Rml::String Nobody = "\xF3\xB0\x84\x90";

	if (!TestTrue(TEXT("a covered string measures at all"), Measure(Narrow, "AA") > 0))
	{
		return false;
	}

	TestEqual(TEXT("a character the face lacks is drawn by the face that has it"),
		Measure(Narrow, Missing), Measure(Wide, Missing));

	TestEqual(TEXT("the borrowed glyph is the only difference the line sees"),
		Measure(Narrow, "A" + Missing + "A"),
		Measure(Narrow, "AA") + Measure(Wide, Missing));

	// A character nobody covers stays with the face that was asked for, so it goes missing the
	// same way everywhere rather than differently per face.
	TestNotEqual(TEXT("a character no face has is not handed off"),
		Measure(Narrow, Nobody), Measure(Wide, Nobody));

	return true;
}

#endif

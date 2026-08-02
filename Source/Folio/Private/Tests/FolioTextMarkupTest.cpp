#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioCore/FolioTerms.h"
#include "FolioCore/FolioTextMarkup.h"

/**
 * The markup grammar, which nothing else can check.
 *
 * A document renders whatever the parser produced, so a rule that quietly changed would show up as
 * a line of dialogue reading slightly wrong in one language, months later. The schedule matters as
 * much as the runs: a delay attached to the wrong character is invisible in a screenshot.
 *
 * The gather script carries a second implementation of this grammar, because it has to validate a
 * translation without an engine. Anything added here belongs there too.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioTextMarkupTest, "Folio.Text.Markup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString Joined(const Folio::TextMarkup::FParsed& Parsed)
	{
		FString Out;
		for (const Folio::TextMarkup::FRun& Run : Parsed.Runs)
		{
			Out += FString::Printf(TEXT("[%s|%s]"),
				*FString(Run.bBreak ? "<br>" : Run.Text.c_str()), *FString(Run.Classes.c_str()));
		}
		return Out;
	}
}

bool FFolioTextMarkupTest::RunTest(const FString&)
{
	using namespace Folio::TextMarkup;

	{
		const FParsed Parsed = Parse("plain words");
		TestEqual(TEXT("one unstyled run"), Joined(Parsed), TEXT("[plain words|]"));
		TestEqual(TEXT("characters counted"), Parsed.CharCount, 11);
		TestEqual(TEXT("no errors"), int32(Parsed.Errors.size()), 0);
	}

	{
		// Indentation in a document is not text. Collapsing here rather than leaving it to layout
		// keeps the parsed characters and the laid-out ones countable against each other.
		const FParsed Parsed = Parse("\n\tspaced   out\n");
		TestEqual(TEXT("whitespace collapses and trims"), Joined(Parsed), TEXT("[spaced out|]"));
	}

	{
		const FParsed Parsed = Parse("You <angry>must</> go");
		TestEqual(TEXT("a style splits the runs"), Joined(Parsed),
			TEXT("[You |][must|angry][ go|]"));
	}

	{
		const FParsed Parsed = Parse("<a><b>both</></>");
		TestEqual(TEXT("nested styles accumulate"), Joined(Parsed), TEXT("[both|a b]"));
	}

	{
		const FParsed Parsed = Parse("<angry>named</angry> close");
		TestEqual(TEXT("a named close is accepted"), Joined(Parsed), TEXT("[named|angry][ close|]"));
		TestEqual(TEXT("and is not an error"), int32(Parsed.Errors.size()), 0);
	}

	{
		// The space between two styles belongs to the run that was open when it arrived, or an
		// underline on the second would start one character early.
		const FParsed Parsed = Parse("a <em>b</> c");
		TestEqual(TEXT("a space keeps the earlier run"), Joined(Parsed), TEXT("[a |][b|em][ c|]"));
	}

	{
		const FParsed Parsed = Parse("one{br}two");
		TestEqual(TEXT("a break is its own run"), Joined(Parsed), TEXT("[one|][<br>|][two|]"));
	}

	{
		const FParsed Parsed = Parse("ab{delay:0.2}cd{/}e");
		if (TestEqual(TEXT("five characters"), Parsed.CharCount, 5))
		{
			TestTrue(TEXT("before the scope runs at the reader's rate"), Parsed.Delays[1] < 0.0f);
			TestEqual(TEXT("inside the scope waits"), Parsed.Delays[2], 0.2f);
			TestEqual(TEXT("and so does the second"), Parsed.Delays[3], 0.2f);
			TestTrue(TEXT("after it, back to the rate"), Parsed.Delays[4] < 0.0f);
		}
	}

	{
		const FParsed Parsed = Parse("a{pause:0.5}b");
		if (TestEqual(TEXT("a pause is not a character"), Parsed.CharCount, 2))
		{
			TestEqual(TEXT("it waits before the next one"), Parsed.Pauses[1], 0.5f);
			TestEqual(TEXT("and not before the one it followed"), Parsed.Pauses[0], 0.0f);
		}
	}

	{
		Folio::FTermTables& Terms = Folio::FTermTables::Get();
		const bool bHasTerm = Terms.Contains("Folio.Test.Term");

		const FParsed Parsed = Parse("go to {tag:Folio.Test.Term} now");
		if (!bHasTerm)
		{
			// No table is loaded in a bare test run, so this is the missing-term path: the tag is
			// left in the text rather than dropped, because text that silently disappears is text
			// nobody reports.
			TestEqual(TEXT("a missing term is reported"), int32(Parsed.Errors.size()), 1);
			TestTrue(TEXT("and stays visible"),
				FString(Parsed.Plain.c_str()).Contains(TEXT("{tag:Folio.Test.Term}")));
		}
		TestEqual(TEXT("the tag is recorded either way"), int32(Parsed.Terms.size()), 1);
	}

	{
		const FParsed Parsed = Parse("2 \\< 3 and \\{ok\\}");
		TestEqual(TEXT("escapes are literal"), Joined(Parsed), TEXT("[2 < 3 and {ok}|]"));
		TestEqual(TEXT("and not errors"), int32(Parsed.Errors.size()), 0);
	}

	{
		const FParsed Parsed = Parse("<open>never closed");
		TestEqual(TEXT("an unclosed style is an error"), int32(Parsed.Errors.size()), 1);
		TestEqual(TEXT("but the text still parses"), Joined(Parsed), TEXT("[never closed|open]"));
	}

	{
		const FParsed Parsed = Parse("{nonsense:1} here");
		TestEqual(TEXT("an unknown directive is an error"), int32(Parsed.Errors.size()), 1);
		TestTrue(TEXT("and is left where the author can see it"),
			FString(Parsed.Plain.c_str()).Contains(TEXT("{nonsense:1}")));
	}

	{
		const FParsed Parsed = Parse("</>");
		TestEqual(TEXT("a close with nothing open is an error"), int32(Parsed.Errors.size()), 1);
	}

	return true;
}

#endif

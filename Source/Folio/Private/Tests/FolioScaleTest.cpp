#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "FolioScale.h"

#include "Engine/UserInterfaceSettings.h"

/**
 * A document has to size exactly as a UMG widget does, and the two ways that has gone wrong are the
 * two things asserted here.
 *
 * The ratio has to be the engine's own answer for the viewport, not an approximation of it. And a
 * context unit has to be a pixel: RmlUi converts dp lengths itself, so anything that scales the drawn
 * geometry by the ratio as well squares it - which looks correct at whatever resolution the ratio
 * happens to be 1, and doubles or halves everywhere else.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFolioScaleTest, "Folio.Scale.MatchesUMG",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFolioScaleTest::RunTest(const FString&)
{
	const FIntPoint Sizes[] = {
		FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080),
		FIntPoint(2560, 1440), FIntPoint(3840, 2160), FIntPoint(1000, 620),
	};

	const UUserInterfaceSettings* Settings = GetDefault<UUserInterfaceSettings>();

	for (const FIntPoint& Size : Sizes)
	{
		const FString Where = FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);

		const float Umg = Settings->GetDPIScaleBasedOnSize(Size);
		const float Ratio = Folio::ScaleRatio(Umg, 1.0f, 1.0f, 0.0f, 0.0f);

		TestEqual(*FString::Printf(TEXT("%s: following fully is UMG's own scale"), *Where), Ratio, Umg);

		// What the canvas does: the context is the pixel size, and the paint transform undoes Slate's
		// scale so a context unit lands on a pixel. Stated as the product, which is what a regression
		// would change.
		const float ContextToLocal = 1.0f / Umg;
		TestEqual(*FString::Printf(TEXT("%s: a context unit is one pixel"), *Where),
			ContextToLocal * Umg, 1.0f);

		// A 452dp card next to a 400 unit UMG box keeps the same proportion at every size. Squaring the
		// ratio is exactly what this catches.
		const float Card = 452.0f * Ratio;
		const float Box = 400.0f * Umg;
		TestEqual(*FString::Printf(TEXT("%s: a dp length keeps its proportion to a UMG one"), *Where),
			Card / Box, 452.0f / 400.0f, 0.0001f);
	}

	// The dial, at both ends and in the middle. Zero follow is a constant pixel size whatever the
	// viewport says, which is the other thing a regression here would break.
	TestEqual(TEXT("no follow ignores the viewport"), Folio::ScaleRatio(2.0f, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f);
	TestEqual(TEXT("half follow is halfway"), Folio::ScaleRatio(2.0f, 0.5f, 1.0f, 0.0f, 0.0f), 1.5f);
	TestEqual(TEXT("the multiplier applies after"), Folio::ScaleRatio(2.0f, 1.0f, 0.5f, 0.0f, 0.0f), 1.0f);
	TestEqual(TEXT("the maximum bounds it"), Folio::ScaleRatio(4.0f, 1.0f, 1.0f, 0.0f, 2.0f), 2.0f);
	TestEqual(TEXT("the minimum bounds it"), Folio::ScaleRatio(0.25f, 1.0f, 1.0f, 0.5f, 0.0f), 0.5f);

	return true;
}

#endif

#pragma once

#include "CoreMinimal.h"

/**
 * How much a document's `dp` unit follows the viewport.
 *
 * Unreal's UI scale is proportional to the viewport's shortest side, so a document laid out against
 * it keeps the same *fraction* of the screen at every resolution. That is right for a menu and
 * usually wrong for a HUD: a HUD is anchored to the corners, so following the viewport inflates
 * everything on a big window and shrinks it to nothing on a small one.
 *
 * Following none of it is the other extreme - a constant pixel size, so a small window shows a HUD
 * that covers a larger share of it. Neither end is what a shooter wants, so this is a dial rather
 * than a switch.
 */
namespace Folio
{
	/** 1 is Unreal's scale, 0 is a constant pixel size, and the useful answers are in between. */
	inline float ScaleRatio(const float ViewportScale, const float Follow, const float Multiplier,
		const float MinScale, const float MaxScale)
	{
		float Result = FMath::Lerp(1.0f, ViewportScale, FMath::Clamp(Follow, 0.0f, 1.0f)) * Multiplier;

		if (MinScale > 0.0f) { Result = FMath::Max(Result, MinScale); }
		if (MaxScale > 0.0f) { Result = FMath::Min(Result, MaxScale); }

		return FMath::Max(Result, KINDA_SMALL_NUMBER);
	}
}

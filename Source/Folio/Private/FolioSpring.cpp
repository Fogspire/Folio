#include "FolioSpring.h"

void FFolioSpring::Advance(const FVector2f Target, const float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float Omega = 2.0f * PI * FMath::Max(0.05f, Frequency);
	const float Zeta = FMath::Max(0.05f, Damping);

	// A step longer than about a fifth of the period diverges rather than settling, so a long frame is
	// split rather than trusted. Capped, because a five-second hitch does not deserve a hundred steps -
	// the spring has settled long before that and the extra work would be spent on nothing.
	const float MaxStep = 0.2f / Omega;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaSeconds / MaxStep), 1, 16);
	const float Step = DeltaSeconds / float(Steps);

	for (int32 Index = 0; Index < Steps; ++Index)
	{
		// Semi-implicit: velocity first, then position from the new velocity. Stable where explicit
		// Euler is not, for one extra line.
		const FVector2f Acceleration =
			(Target - Value) * (Omega * Omega) - Velocity * (2.0f * Zeta * Omega);

		Velocity += Acceleration * Step;
		Value += Velocity * Step;
	}
}

#pragma once

#include "CoreMinimal.h"

#include "FolioSpring.generated.h"

/**
 * A critically-dampable second-order spring, integrated per frame.
 *
 * This is the thing a transition cannot be. A transition fires on a state change and runs a fixed
 * curve to completion; a HUD that trails a moving camera is responding continuously to a signal that
 * never settles, and gets kicked again before it arrives. That needs velocity carried between frames,
 * which RCSS has nowhere to put.
 *
 * Stiffness and damping are expressed as frequency and a damping ratio rather than as spring
 * constants, because those are the two numbers anyone actually tunes: how fast it responds, and
 * whether it overshoots.
 */
USTRUCT(BlueprintType)
struct FOLIO_API FFolioSpring
{
	GENERATED_BODY()

	/**
	 * How quickly it responds, in oscillations per second.
	 *
	 * Around 2 is a HUD that feels attached; 6 or more is nearly rigid and barely worth the spring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0.05"))
	float Frequency = 2.5f;

	/**
	 * Below 1 overshoots and settles, 1 is the fastest approach without overshoot, above 1 is sluggish.
	 *
	 * Slightly under 1 is what reads as physical: a helmet that overshoots a little on a hard turn
	 * looks like it has mass, where a critically damped one looks like it is being interpolated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Folio", meta = (ClampMin = "0.05"))
	float Damping = 0.7f;

	/** Current value, in whatever unit the caller is driving. */
	FVector2f Value = FVector2f::ZeroVector;

	/** Carried between frames, which is the whole reason this exists rather than a lerp. */
	FVector2f Velocity = FVector2f::ZeroVector;

	/**
	 * Advances towards Target.
	 *
	 * Semi-implicit Euler, substepped so a long frame cannot make the spring explode. An explicit
	 * integrator at 4 Hz on a 200ms hitch diverges rather than settling, which shows up as the HUD
	 * flying off screen exactly when the game is already struggling.
	 */
	void Advance(FVector2f Target, float DeltaSeconds);

	/** Puts it at rest at a value, for a teleport or a fresh document. */
	void Snap(FVector2f To)
	{
		Value = To;
		Velocity = FVector2f::ZeroVector;
	}

	/** Whether it has effectively settled, so a caller can stop writing and let the document idle. */
	bool IsAtRest(FVector2f Target, float Tolerance = 0.05f) const
	{
		return (Value - Target).IsNearlyZero(Tolerance) && Velocity.IsNearlyZero(Tolerance);
	}
};

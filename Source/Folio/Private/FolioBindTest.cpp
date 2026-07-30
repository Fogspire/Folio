#include "FolioBindTest.h"

void UFolioBindTest::Advance()
{
	++Ticks;

	Health = 0.5f + 0.5f * FMath::Sin(Ticks * 0.05f);
	bAlive = Health > 0.35f;
	Status = bAlive ? TEXT("alive") : TEXT("down");
}

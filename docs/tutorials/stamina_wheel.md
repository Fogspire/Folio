# A stamina wheel

Stamina represented on a wheel, with partial, extra and temporary wheels. It appears when stamina is spent, empties clockwise with what was just spent trailing in red, turns red when run dry, and fades a moment after it is full.

The markup is the `stamina_wheel.rml` recipe. Your game only supplies numbers: where the wheel is, and for each ring how much is filled.

```bash
./bin/win64/uipreview.exe rcss/recipes/stamina_wheel.rml --root . --size 1080x1080 --strict
```

---

## 1. Put the wheel in your HUD

Copy the recipe's `.stamina` rules and its `<div data-model="stamina">` block into your HUD document. A HUD has other things in it, so the wheel is not a document of its own; the recipe's body rules are only for previewing it alone.

The model it binds:

| Name | Type | |
|---|---|---|
| `bShown` | bool | The wheel is up. It fades in and out on its own. |
| `Position` | string | `translate(Xdp, Ydp)`, the wheel's centre on screen, optionally followed by `scaleX(S)` to angle it and `rotate(Ndeg)` to spin it |
| `Wheels` | array | One row per ring, innermost first |

Each row of `Wheels`:

| Name | Type | |
|---|---|---|
| `Size` | float | Outer diameter, dp |
| `Hole` | float | The cut-out as a percentage of the radius. Smaller is a thicker ring |
| `Length` | float | Degrees of the ring that exist, 0 to 360. Less than 360 is a part ring, with no track past it |
| `Fill` | float | Degrees filled, 0 to `Length` |
| `Trail` | float | Degrees the red end reaches, `Fill` to `Length` |
| `bSpent` | bool | Run dry: the fill is red until it recovers |
| `bTemporary` | bool | Bonus stamina: yellow |

## 2. The view model

```cpp
USTRUCT(BlueprintType)
struct FStaminaWheel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float Size = 60.f;
	UPROPERTY(BlueprintReadOnly) float Hole = 36.f;
	UPROPERTY(BlueprintReadOnly) float Length = 360.f;
	UPROPERTY(BlueprintReadOnly) float Fill = 360.f;
	UPROPERTY(BlueprintReadOnly) float Trail = 360.f;
	UPROPERTY(BlueprintReadOnly) bool bSpent = false;
	UPROPERTY(BlueprintReadOnly) bool bTemporary = false;
};

UCLASS()
class UStaminaViewModel : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly) bool bShown = false;
	UPROPERTY(BlueprintReadOnly) FString Position;
	UPROPERTY(BlueprintReadOnly) TArray<FStaminaWheel> Wheels;
};
```

Bind it once, after the document is created, and change it from then on:

```cpp
Model = NewObject<UStaminaViewModel>(this);
Document->BindViewModel(TEXT("stamina"), Model, true);
```

## 3. Fill it every frame

Stamina comes from wherever your game keeps it. Here it is `Stamina` out of `MaxStamina`, with `PerWheel` stamina per ring, `bDrained` for run dry, and `Bonus` for temporary stamina.

```cpp
void UMyHud::UpdateStamina(float DeltaTime)
{
	const float Total = MaxStamina + Bonus;

	// What was just spent trails in red, catching up quickly: a faster drain shows a longer red end.
	Trail = Stamina + Bonus >= Trail || bDrained ? Stamina + Bonus : FMath::FInterpTo(Trail, Stamina + Bonus, DeltaTime, 6.f);

	// Up while short, and a second more once full so the refill is seen to finish.
	FullTime = (Stamina >= MaxStamina && !bDrained) ? FullTime + DeltaTime : 0.f;
	Model->bShown = FullTime < 1.f;

	// One ring per PerWheel of stamina, the last a part ring if the total does not divide evenly. 1.6 wheels is
	// a whole ring and three fifths of the next.
	const int32 Count = FMath::Max(1, FMath::CeilToInt(Total / PerWheel));
	Model->Wheels.SetNum(Count);
	float Size = 60.f;
	float Inner = 0.f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FStaminaWheel& Wheel = Model->Wheels[Index];
		const float Start = Index * PerWheel;
		Wheel.Size = Size;
		// The first ring is thick; the rest are thin bands just outside the one before.
		Wheel.Hole = Index == 0 ? 36.f : (Inner + 3.f) / Size * 100.f;
		Wheel.Length = FMath::Clamp((Total - Start) / PerWheel, 0.f, 1.f) * 360.f;
		Wheel.Fill = FMath::Clamp((Stamina + Bonus - Start) / PerWheel, 0.f, 1.f) * 360.f;
		Wheel.Trail = FMath::Max(Wheel.Fill, FMath::Clamp((Trail - Start) / PerWheel, 0.f, 1.f) * 360.f);
		Wheel.bSpent = bDrained;
		// A ring that is all bonus is yellow; one straddling the two stays the stamina colour.
		Wheel.bTemporary = Start >= MaxStamina;
		Inner = Size;
		Size += 16.f;
	}

	// Beside the head, as the screen sees it.
	FVector2D Pixel;
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
	if (Scale > 0.f && PC->ProjectWorldLocationToScreen(Character->GetActorLocation() + FVector(0, 0, 120), Pixel, true))
	{
		const FVector2D At = Pixel / Scale + FVector2D(64.f, -28.f);
		Model->Position = FString::Printf(TEXT("translate(%.0fdp, %.0fdp) scaleX(%.3f) rotate(%.0fdeg)"), At.X, At.Y, FMath::Cos(FMath::DegreesToRadians(Tilt)), Angle);
	}
}
```

`Trail` and `FullTime` are members, starting at the full amount and 0; `Tilt` and `Angle` are settings, both 0 to leave it facing the screen and filling from twelve.

A data model is polled, so setting the fields is all it takes. Keep `Wheels` the same length from frame to frame where you can: a row removed is destroyed on the spot, and one added is built.

## When it goes wrong

- **The rings are whole discs:** the mask is not being applied. It masks decorators, not `background-color`, so a ring that has been restyled with a plain background shows solid.
- **The wheel never shows:** check `bShown` with your HUD's stats, then that stamina is really dropping. Movement components often only spend stamina once the character is near full sprint speed, which can be seconds after sprint is pressed.
- **It shows, then vanishes straight away:** stamina is hovering just under full, from a correction or a regen that never quite reaches the maximum. Compare against full with a little slack.
- **It sits in the wrong place:** `Position` is in dp, so divide the projected pixels by the viewport scale first.
- **A part ring draws a whole track:** `Length` is still 360. It is the share of that ring the maximum reaches, not the share filled.
- **The outer rings overlap the inner:** a ring's `Hole` has to leave its inside edge outside the ring before it: `Hole` of at least `previous Size / Size * 100`.

## Tune it

- **Thickness:** `Hole` on the first ring. 36 is a thick ring; 60 is a thin band.
- **Tilt:** `Tilt` angles the wheel as if turned about its vertical axis toward the character, which makes it read as attached to them. It is a horizontal squash by the angle's cosine: `perspective()` and `rotateY()` do not reach the Unreal renderer, which is 2D.
- **Where it fills from:** `Angle`, degrees clockwise from twelve.
- **Part rings:** `PerWheel` against the maximum. A maximum of 1.6 wheels' worth draws one ring and three fifths of the next.
- **Size:** `Size`, and the 16dp each further ring adds.
- **Colours:** the hex values in the recipe's `data-style-decorator` expression: fill, red end, temporary, and the track.
- **How long the red end lingers:** the 6 in the `FInterpTo`. Lower lingers longer.
- **How long it stays once full:** the 1 second in `bShown`. The fade itself is the 0.3s `transition` on `.stamina`.
- **Where it sits:** the offset added to the projected position.

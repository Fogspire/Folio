# Subtitles and closed captions

A stack of spoken lines at the foot of the frame, newest at the bottom, each on its own clock.

It is the only arrangement that survives two people talking over each other. A few lines at a time, the newest bright and the ones above it dimmed, each leaving when it has had its say rather than when the next one arrives.

`rcss/recipes/subtitles.rml` is a complete one. Copy it into your `UI/screens/` and change the palette and the type.

## The document draws; the queue is yours

The queue is most of the feature. The document reads a list and knows nothing about time, sound or speakers. Everything below lives in your game.

```cpp
USTRUCT()
struct FMySubtitleLine
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString Speaker;
    UPROPERTY(BlueprintReadOnly) bool    bHasSpeaker = false;
    UPROPERTY(BlueprintReadOnly) FString Text;

    /** Which speaker colour, as an index the stylesheet turns into a colour. */
    UPROPERTY(BlueprintReadOnly) int32   Voice = 0;

    /** The most recent line. Everything above it is dimmed. */
    UPROPERTY(BlueprintReadOnly) bool    bFresh = false;

    /** Past its time and playing its way off. See "Leaving" below - this one is not optional. */
    UPROPERTY(BlueprintReadOnly) bool    bLeaving = false;

    /** A sound rather than speech, which a closed caption sets apart. */
    UPROPERTY(BlueprintReadOnly) bool    bCue = false;
};
```

The model itself holds `TArray<FMySubtitleLine> Lines`, a `bool bAny`, and a `float Offset` that moves the stack up the frame as a fraction of the viewport.

## How long a line holds

**Off the written length, not off the sound.** A voice line's audio has a breath in front, a tail behind and often a second of room tone, so a five word line arrives with six seconds of sound attached.

```cpp
float ReadingTime(const FString& Text)
{
    // A floor because the eye needs longer to notice a caption appear than to read a short one once
    // noticed, and a ceiling because a caption that outstays its moment stops being read and starts
    // being scenery.
    return FMath::Clamp(1.4f + Text.Len() * 0.045f, 1.8f, 7.0f);
}
```

Per character rather than per word, because a Japanese or Chinese line has no spaces, and counting words gives a caption that is gone before it is seen.

**Drop the oldest rather than refusing the newest** when the stack is full. The latest line is what the player most needs, and a queue that fills up and stops, stops during a fight.

## Leaving

**A row taken out of a data array is destroyed immediately, so a line cannot animate its own removal.** Removing the entry the frame its time runs out gives no exit at all: one frame of something, then nothing.

Hold it in the list instead:

```cpp
for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
{
    FLine& Line = Lines[Index];
    Line.Left -= DeltaSeconds;

    if (Line.Left > 0.f)
    {
        continue;
    }

    if (!Line.bLeaving)
    {
        Line.bLeaving = true;     // the document hangs its exit on this
        Line.Left = ExitSeconds;  // and it has to agree with the RCSS
        continue;
    }

    Lines.RemoveAt(Index);
}
```

Two consequences:

- **A leaving line should stop counting toward the maximum.** Otherwise a new line cuts a departing one's exit short.
- **A leaving line is nobody's newest**, even when it is the only one left, or it holds the bright styling all the way out.

## Do not dim or fade with opacity

Opacity is inherited in RCSS, unlike CSS, and multiplied into premultiplied vertex colours rather than compositing the subtree as one image. A caption is a dark slab with light type over it, so a fading one blows out to white where the two overlap.

| | |
|---|---|
| Ageing a line | A quieter ground and quieter type. `.sub.old { background-color: ... }` |
| Taking one away | A transform. The recipe wipes with `scaleX`, from the edge the tick is on |

`filter` would composite it properly but costs a render target every frame.

## Colour is not identity

**Always write the speaker's name.** Captions are read by exactly the players least served by leaning on hue. Colour reinforces the name; it never replaces it.

With more voices than colours, wrap the palette. Two characters sharing a colour is fine when both are named.

**Claim a voice's colour when it comes into the world, not when it speaks.** A colour handed out per line is given back when the caption expires, so a character who speaks twice with a pause between reads as two people. Use a round robin over the palette, wrapping, held for the lifetime of whatever owns the voice.

## What the recipe does not do

Two things worth adding, deliberately left out because they are not part of a caption:

- **A direction arrow**, pointing at the speaker. Useful where hearing someone you cannot see is the point. Rotate it by the flat bearing from the *player's body* to the speaker, taken against the camera's yaw. Measuring from the camera puts it several metres behind the player in third person, which is most wrong at close range.

  Build the arrowhead from solid bars. Collapsing a box and letting its borders mitre into a triangle draws correctly at every quarter turn and upside down at every bearing between.

- **A marker over the speaker's head**, in the line's colour. See [markers](markers.md); the shared colour links caption and head without explanation.

## Where to go next

- **[Data binding](data_binding.md)**: driving the list from gameplay.
- **[Markers](markers.md)**: the mark over the speaker's head.
- **[Accessibility](accessibility.md)**: the contrast and text-scale checks a caption has to pass.
